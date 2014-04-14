#include "DtiRegistration.h"

DtiRegistration::DtiRegistration(QString module) : Script(module)
{
   m_grid_suffix = "-grid"; 
   m_upsampling_suffix = "-upsampled";
   m_stripping_suffix = "-stripped";
   m_registering_suffix = "-registered";
}

void DtiRegistration::initializeScript()
{
   definePython();
   importGeneralModules();

   m_script += "import time \n";
   m_script += "import array \n\n";

   defineExecutable("ANTS");
   defineExecutable("ResampleVolume2");
   defineExecutable("ITKTransformTools");
   defineExecutable("dtiestim");
   defineExecutable("ImageMath");
   defineExecutable("bet2");
   defineExecutable("dtiprocess");
   defineExecutable("HistogramMatching");

   m_script += "logger = logging.getLogger('NeosegPipeline')\n\n";

   m_script += "runningProcess = None\n\n";  
}

void DtiRegistration::upsample(QString image)
{
   // Computing the grid 
   QString grid_name = m_prefix + image + m_grid_suffix + m_suffix + ".nrrd";
   QString grid_path = m_module_dir->filePath(grid_name); 

   m_log = "- Computing grid of the " + image + " upsampled";
   m_outputs.insert("grid" + image, grid_path); 
   m_argumentsList << "ITKTransformTools" << "'size'" << image << "'-'" << "'--grid'" << "grid" + image << "'--spacing'" << "'1'" << "'1'" << "'1'"; 
   execute();

   m_unnecessaryFiles << grid_path;

   // Upsampling 
   QString upsampledImage_name = m_prefix + image + m_upsampling_suffix + m_suffix + ".nrrd";
   QString upsampledImage_path = m_module_dir->filePath(upsampledImage_name); 

   m_log = "- Upsampling " + image;
   m_outputs.insert("upsampled" + image, upsampledImage_path); 
   m_argumentsList << "ResampleVolume2" << "'-R'" << "grid" + image << "'-i'" << "'linear'" << image << "upsampled" + image; 
   execute();

   m_unnecessaryFiles << upsampledImage_path;
}

void DtiRegistration::generateDTI()
{
   QString DTI_name = m_prefix + "DTI" + m_suffix + ".nrrd";
   QString DTI_path = m_module_dir->filePath(DTI_name); 

   m_log = "- Generating DTI";
   m_outputs.insert("DTI", DTI_path);
   m_argumentsList << "dtiestim" << "'--dwi_image'" << "DWI" << "'--B0'" << "b0" << "'--tensor_output'" << "DTI"; 
   execute(); 

   m_unnecessaryFiles << DTI_path;
}

void DtiRegistration::skullStripb0()
{
   // Converting b0 in nifti
   QString b0Nifti_name = m_prefix + "b0" + m_suffix + ".nii.gz";
   QString b0Nifti_path = m_module_dir->filePath(b0Nifti_name); 

   m_log = "- Converting the b0 in nifti";
   m_outputs.insert("b0Nifti", b0Nifti_path); 
   m_argumentsList << "ResampleVolume2" << "b0" << "b0Nifti"; 
   execute();

   m_unnecessaryFiles << b0Nifti_path;

   // Skull-stripping b0 
   QString outbase_name = m_prefix + "b0" + m_suffix;
   QString outbase_path = m_module_dir->filePath(outbase_name); 

   QString b0Mask_name = m_prefix + "b0" + m_suffix + "_mask.nii.gz";
   QString b0Mask_path = m_module_dir->filePath(b0Mask_name); 

   QString strippedb0_name = m_prefix + "b0" + m_suffix + ".nii.gz";
   QString strippedb0_path = m_module_dir->filePath(strippedb0_name); 

   m_log = "- Skull-stripping the b0";
   m_inputs.insert("outbase", outbase_path); 
   m_outputs.insert("b0Mask", b0Mask_path);
   m_outputs.insert("strippedb0", strippedb0_path); 
   m_argumentsList << "bet2" << "b0Nifti" << "outbase" << "'-m'";
   execute();

   m_unnecessaryFiles << b0Mask_path;
   m_unnecessaryFiles << strippedb0_path;
}

void DtiRegistration::skullStripDTI()
{
   // Skull-stripping DTI 
   QString strippedDTI_name = m_prefix + "DTI" + m_stripping_suffix + m_suffix + ".nrrd";
   QString strippedDTI_path = m_module_dir->filePath(strippedDTI_name); 

   m_log = "- Skull-stripping DTI";
   m_outputs.insert("strippedDTI", strippedDTI_path); 
   m_argumentsList << "ImageMath" << "DTI" << "'-mask'" << "b0Mask" << "'-outfile'" << "strippedDTI"; 
   execute(); 

   m_unnecessaryFiles << strippedDTI_path;
}

void DtiRegistration::generateFA()
{
   // Generating FA
   QString FA_name = m_prefix + "FA" + m_suffix + ".nrrd"; 
   QString FA_path = m_module_dir->filePath(FA_name); 

   m_log = "- Generating FA";
   m_outputs.insert("FA", FA_path);
   m_argumentsList << "dtiprocess" << "'--dti_image'" << "strippedDTI" << "'--fa_output'" << "FA"; 
   execute();

   m_unnecessaryFiles << FA_path;
}

void DtiRegistration::generateAD()
{
   // Generating AD 
   QString AD_name = m_prefix + "AD" + m_suffix + ".nrrd";
   QString AD_path = m_module_dir->filePath(AD_name); 

   m_log = "- Generating AD"; 
   m_outputs.insert("AD", AD_path); 
   m_argumentsList << "dtiprocess" << "'--dti_image'" << "strippedDTI" << "'--lambda1_output'" << "AD"; 
   execute();

   m_unnecessaryFiles << AD_path;
}

void DtiRegistration::calculateTransformations()
{
   // Calculating transformations
   QString modality1 = "CC[' + T2 + ',' + strippedb0 + ',1,2]";
   QString modality2 = "CC[' + T2 + ',' + upsampledAD + ',1,2]";

   QString output_name = "DTI_to_" + m_prefix + ".nii.gz";
   QString output_path = m_module_dir->filePath(output_name); 

   QString affine_name = "DTI_to_" + m_prefix + "Affine.txt";
   QString affine_path = m_module_dir->filePath(affine_name);

   QString warp_name = "DTI_to_" + m_prefix + "Warp.nii.gz";
   QString warp_path = m_module_dir->filePath(warp_name);

   QString inverseWarp_name = "DTI_to_" + m_prefix + "InverseWarp.nii.gz"; 
   QString inverseWarp_path = m_module_dir->filePath(inverseWarp_name);

   m_log = "- Calculating transformations";
   m_inputs.insert("modality1", modality1); 
   m_inputs.insert("modality2", modality2); 
   m_inputs.insert("output", output_path);
   m_outputs.insert("affine", affine_path); 
   m_outputs.insert("warp", warp_path);
   m_argumentsList << "ANTS" << "'3'" << "'-m'" << "modality1" << "'-m'" << "modality2" << "'-o'" << "output" << "'-i'" << "'30x20x10'" << "'-r'" << "'Gauss[5,5]'" << "'-t'" << "'SyN[0.125]'"; 
   execute(); 

   m_unnecessaryFiles << affine_path;
   m_unnecessaryFiles << affine_path + "~";
   m_unnecessaryFiles << warp_path;
   m_unnecessaryFiles << inverseWarp_path;

   // Normalizing affine file    
   m_log = "- Normalizing affine file";
   m_argumentsList << "'text_subst.pl'" << "'MatrixOffsetTransformBase_double_3_3'" << "'AffineTransform_double_3_3'" << "affine"; 
   execute();
}

void DtiRegistration::applyTransformations(QString image)
{
   // Applying transformations 
   QString registeredImage_name = m_prefix + image + m_registering_suffix + m_suffix + ".nrrd";
   QString registeredImage_path = m_module_dir->filePath(registeredImage_name); 

   m_log = "- Applying transformations to " + image;
   m_outputs.insert("registered" + image, registeredImage_path); 
   m_argumentsList << "ResampleVolume2" << "upsampled" + image << "registered" + image << "'--Reference'" << "T1" << "'-i'" << "'linear'" << "'--hfieldtype'" << "'displacement'" << "'--defField'" << "warp" << "'--transformationFile'" << "affine"; 
   execute(); 

   if(image == "FA")
   {
      m_neo.FA = registeredImage_path;
   }
   if(image == "AD")
   {
      m_neo.AD = registeredImage_path;
   }
}

void DtiRegistration::implementRun()
{   
   m_script += "def run():\n\n";

   m_script += "\tsignal.signal(signal.SIGINT, stop)\n";
   m_script += "\tsignal.signal(signal.SIGTERM, stop)\n\n";

   m_script += "\tlogger.info('=== DTI Registration ===')\n";

   QString registeredAD_name = m_prefix + "AD" + m_registering_suffix + m_suffix + ".nrrd";
   QString registeredAD_path = m_module_dir->filePath(registeredAD_name); 

   QString registeredFA_name = m_prefix + "FA" + m_registering_suffix + m_suffix + ".nrrd";
   QString registeredFA_path = m_module_dir->filePath(registeredFA_name); 

   m_outputs.insert("finalAD", registeredAD_path); 
   m_outputs.insert("finalFA", registeredFA_path); 
   checkFinalOutputs(); 

   m_script += "\tlogger.info('')\n";

   m_script += "\tT1 = '" + m_neo.T1 + "'\n";
   m_script += "\tT2 = '" + m_neo.T2 + "'\n";
   m_script += "\tDWI = '" + m_neo.DWI + "'\n";
   m_script += "\tb0 = '" + m_neo.b0 + "'\n";

   m_script += "\n\t# Upsample b0 #\n";
   upsample("b0"); 

   m_script += "\n\t# Generate DTI #\n";
   generateDTI(); 

   m_script += "\n\t# Skull-Strip b0 #\n";
   skullStripb0(); 

   m_script += "\n\t# Skull-Strip DTI #\n";
   skullStripDTI(); 

   m_script += "\n\t# Generate FA #\n";  
   generateFA(); 

   m_script += "\n\t# Upsample FA #\n";
   upsample("FA"); 

   m_script += "\n\t# Generate AD #\n"; 
   generateAD(); 

   m_script += "\n\t# Upsample AD #\n";
   upsample("AD"); 

   m_script += "\n\t# Registering FA and AD #\n";
   calculateTransformations(); 
   applyTransformations("FA"); 
   applyTransformations("AD"); 

   m_script += "\tlogger.info('')\n";
}

void DtiRegistration::update()
{
   initializeScript();

   implementStop();
   implementCheckFileExistence();
   implementExecute(); 
   implementExecutePipe(); 

   implementRun();

   writeScript();
}

Neo DtiRegistration::getOutput()
{
   return m_neo;
}


