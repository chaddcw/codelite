#include "project.h"
#include "environmentconfig.h"
#include "evnvarlist.h"
#include "environmentconfig.h"
#include "builder_gnumake.h"
#include "configuration_mapping.h"
#include "dirsaver.h"
#include "wx/tokenzr.h"
#include "macros.h"
#include "build_settings_config.h"
#include "buildmanager.h"
#include "wx/sstream.h"
#include "globals.h"

static bool IsSource(const wxString &ext)
{
	wxString e(ext);
	e = e.MakeLower();
	return e == wxT("cpp") || e == wxT("cxx") || e == wxT("c") || e == wxT("c++") || e == wxT("cc");
}

static bool IsResource(const wxString &ext)
{
	wxString e(ext);
	e = e.MakeLower();
	return e == wxT("rc");
}

BuilderGnuMake::BuilderGnuMake()
		: Builder(wxT("GNU makefile for g++/gcc"), wxT("make"), wxT("-f"))
{
}

BuilderGnuMake::~BuilderGnuMake()
{
}

bool BuilderGnuMake::Export(const wxString &project, bool isProjectOnly, bool force, wxString &errMsg)
{
	if (project.IsEmpty()) {
		return false;
	}
	
	ProjectPtr proj = WorkspaceST::Get()->FindProjectByName(project, errMsg);
	if (!proj) {
		errMsg << wxT("Cant open project '") << project << wxT("'");
		return false;
	}
	
	wxArrayString depsArr = proj->GetDependencies();
	wxArrayString removeList;
	if(!isProjectOnly){
		//this function assumes that the working directory is located at the workspace path
		//make sure that all dependencies exists
		for (size_t i=0; i<depsArr.GetCount(); i++) {
			ProjectPtr dependProj = WorkspaceST::Get()->FindProjectByName(depsArr.Item(i), errMsg);
			//Missing dependencies project?
			//this can happen if a project was removed from the workspace, but
			//is still on the depdendencie list of this project
			if (!dependProj) {
				wxString msg;
				msg << wxT("CodeLite can not find project '") << depsArr.Item(i) << wxT("' which is required\n");
				msg << wxT("for building project '") << project << wxT("'.\nWould like to remove it from the dependency list?");
				if (wxMessageBox(msg, wxT("CodeLite"), wxYES_NO | wxICON_QUESTION) == wxYES) {
					//remove the project from the dependecie list, and continue
					removeList.Add(depsArr.Item(i));
				}
			}
		}
		bool settingsChanged(false);
		//remove the unfound projects from the dependencies array
		for (size_t i=0; i<removeList.GetCount(); i++) {
			int where = depsArr.Index(removeList.Item(i));
			if (where != wxNOT_FOUND) {
				depsArr.RemoveAt(where);
				settingsChanged = true;
			}
		}
		//update the project dependencies a
		bool modified = proj->IsModified();
		proj->SetDependencies(depsArr);
		
		//the set settings functions marks the project as 'modified' this causes 
		//an unneeded makefile generation if the settings was not really modified
		if(!modified && !settingsChanged) {
			proj->SetModified(false);
		}
	}

	wxString fn;
	fn << WorkspaceST::Get()->GetName()  << wxT("_wsp.mk");
	wxString text;

	wxFileName wspfile(WorkspaceST::Get()->GetWorkspaceFileName());

	text << wxT(".PHONY: clean All\n\n");
	text << wxT("All:\n");

	//iterate over the dependencies projects and generate makefile
	wxString buildTool = BuildManagerST::Get()->GetSelectedBuilder()->GetBuildToolCommand(false);
	buildTool = WorkspaceST::Get()->ExpandVariables(buildTool);

	//generate the makefile for the selected workspace configuration
	BuildMatrixPtr matrix = WorkspaceST::Get()->GetBuildMatrix();
	wxString workspaceSelConf = matrix->GetSelectedConfigurationName();

	wxString args;
	if(!isProjectOnly){
		for (size_t i=0; i<depsArr.GetCount(); i++) {
			ProjectPtr dependProj = WorkspaceST::Get()->FindProjectByName(depsArr.Item(i), errMsg);
			if (!dependProj) {
				continue;
			}
			BuildConfigPtr dependProjbldConf = WorkspaceST::Get()->GetProjSelBuildConf(dependProj->GetName());

			GenerateMakefile(dependProj, force);
			wxString projectSelConf = matrix->GetProjectSelectedConf(workspaceSelConf, dependProj->GetName());
			args = wxT("type=");
			args << NormalizeConfigName(projectSelConf) << wxT(" ");
			text << wxT("\t@echo ----------Building project:[ ") << dependProj->GetName() << wxT(" - ") << projectSelConf << wxT(" ]----------\n");

			//make the paths relative
			wxFileName fn(dependProj->GetFileName());
			fn.MakeRelativeTo(wspfile.GetPath());

			text << wxT("\t") << GetCdCmd(wspfile, fn) << buildTool << wxT(" \"") << dependProj->GetName() << wxT(".mk\" ") << args << wxT("\n");
		}
	}
	
	//generate makefile for the project itself
	GenerateMakefile(proj, force);
	wxString projectSelConf = matrix->GetProjectSelectedConf(workspaceSelConf, project);
	args = wxT("type=");
	args << NormalizeConfigName(projectSelConf) << wxT(" ");
	text << wxT("\t@echo ----------Building project:[ ") << project << wxT(" - ") << projectSelConf << wxT(" ]----------\n");

	//make the paths relative
	wxFileName projectPath(proj->GetFileName());
	projectPath.MakeRelativeTo(wspfile.GetPath());
	text << wxT("\t") << GetCdCmd(wspfile, projectPath) << buildTool << wxT(" \"") << proj->GetName() << wxT(".mk\" ") << args << wxT("\n");

	//create the clean target
	text << wxT("clean:\n");
	if(!isProjectOnly){
		for (size_t i=0; i<depsArr.GetCount(); i++) {
			wxString projectSelConf = matrix->GetProjectSelectedConf(workspaceSelConf, depsArr.Item(i));
			args = wxT("type=");
			args << NormalizeConfigName(projectSelConf) << wxT(" ");

			ProjectPtr dependProj = WorkspaceST::Get()->FindProjectByName(depsArr.Item(i), errMsg);
			//Missing dependencies project? just skip it
			if (!dependProj) {
				continue;
			}

			text << wxT("\t@echo ----------Building project:[ ") << dependProj->GetName() << wxT(" - ") << projectSelConf << wxT(" ]----------\n");

			//make the paths relative
			wxFileName fn(dependProj->GetFileName());
			fn.MakeRelativeTo(wspfile.GetPath());

			//if the dependencie project is project of type 'Custom Build' - do the custom build instead
			//of the geenrated makefile
			BuildConfigPtr dependProjbldConf = WorkspaceST::Get()->GetProjSelBuildConf(dependProj->GetName());
			text << wxT("\t") << GetCdCmd(wspfile, fn) << buildTool << wxT(" \"") << dependProj->GetName() << wxT(".mk\" ") << args << wxT(" clean\n") ;
		}
	}
	
	//generate makefile for the project itself
	projectSelConf = matrix->GetProjectSelectedConf(workspaceSelConf, project);
	args = wxT("type=");
	args << NormalizeConfigName(projectSelConf) << wxT(" ");
	text << wxT("\t@echo ----------Building project:[ ") << project << wxT(" - ") << projectSelConf << wxT(" ]----------\n");

	//make the paths relative
	text << wxT("\t") << GetCdCmd(wspfile, projectPath) << buildTool << wxT(" \"") << proj->GetName() << wxT(".mk\" ") << args << wxT(" clean\n") ;

	//dump the content to file
	wxFileOutputStream output(fn);
	wxStringInputStream content(text);
	output << content;
	return true;
}

void BuilderGnuMake::GenerateMakefile(ProjectPtr proj, bool force)
{
	ProjectSettingsPtr settings = proj->GetSettings();
	if (!settings) {
		return;
	}
	
	//get the selected build configuration for this project
	BuildConfigPtr bldConf = WorkspaceST::Get()->GetProjSelBuildConf(proj->GetName());
	if (!bldConf) {
		return;
	}

	wxString path = proj->GetFileName().GetPath();

	DirSaver ds;
	//change directory to the project base dir
	::wxSetWorkingDirectory(path);


	//create new makefile file
	wxString fn(path);
	fn << PATH_SEP << proj->GetName() << wxT(".mk");
	
	//skip the next test if the makefile does not exist
	if(wxFileName::FileExists( fn )) {
		if(!force) {
			if(proj->IsModified() == false) {
				return;
			}
		}
	}
	
	//generate the selected configuration for this project
	//wxTextOutputStream text(output);
	wxString text;

	text << wxT("##") << wxT("\n");
	text << wxT("## Auto Generated makefile, please do not edit") << wxT("\n");;
	text << wxT("##") << wxT("\n");

	//----------------------------------------------------------
	//copy environment variables to the makefile
	//----------------------------------------------------------
	EvnVarList vars;
	EnvironmentConfig::Instance()->ReadObject(wxT("Variables"), &vars);
	StringMap varMap = vars.GetVariables();
	StringMap::const_iterator iter = varMap.begin();
	
	for (; iter != varMap.end(); iter++) {
		wxString name = iter->first;
		wxString value = iter->second;
		text << name << wxT(":=") << value << wxT("") << wxT("\n");
	}
	
	//create a variable for the project name as well
	text << wxT("ProjectName:=") << proj->GetName() << wxT("\n");
	text << wxT("\n");

	CreateConfigsVariables(proj, bldConf, text);

	// create a list of objects
	CreateObjectList(proj, text);

	//-----------------------------------------------------------
	// create the build targets
	//-----------------------------------------------------------
	text << wxT("##\n");
	text << wxT("## Main Build Tragets \n");
	text << wxT("##\n");

	//incase project is type exe or dll, force link
	//this is to workaround bug in the generated makefiles
	//which causes the makefile to report 'nothing to be done'
	//even when a dependencie was modified
	wxString targetName(bldConf->GetIntermediateDirectory());

	//dont always add the 'PrePreBuild' dependenice, or else it will
	//lead to constant archiving of static libraries, even when there is
	//"Nothing to be done for 'all'" :D
	wxString prePreBuildTarget(wxT("PrePreBuild"));
	if (bldConf->GetPreBuildCustom().IsEmpty()) {
		prePreBuildTarget = wxEmptyString;
	}

	if (settings->GetProjectType() == Project::EXECUTABLE || settings->GetProjectType() == Project::DYNAMIC_LIBRARY) {
		text << wxT("all: $(OutputFile)\n\n");
		if ( HasPrebuildCommands(bldConf) ) {
			text << wxT("$(OutputFile): makeDirStep ") << prePreBuildTarget << wxT(" PreBuild $(Objects)\n");
		} else {
			text << wxT("$(OutputFile): makeDirStep ") << prePreBuildTarget << wxT(" $(Objects)\n");
		}
		targetName = wxT("makeDirStep");
	} else {
		text << wxT("all: $(IntermediateDirectory) $(OutputFile)\n\n");
		if (HasPrebuildCommands(bldConf)) {
			text << wxT("$(OutputFile): ") << prePreBuildTarget << wxT(" PreBuild $(Objects)\n");
		} else {
			text << wxT("$(OutputFile): ") << prePreBuildTarget << wxT(" $(Objects)\n");
		}
	}

	CreateTargets(proj->GetSettings()->GetProjectType(), bldConf, text);
	CreatePostBuildEvents(bldConf, text);
	CreateMakeDirsTarget(bldConf, targetName, text);
	CreatePreBuildEvents(bldConf, text);

	//-----------------------------------------------------------
	// Create a list of targets that should be built according to
	// projects' file list
	//-----------------------------------------------------------
	CreateFileTargets(proj, text);

	//dump the content to a file
	wxFFile output;
	output.Open(fn, wxT("w+"));
	if(output.IsOpened()){
		output.Write(text);
		output.Close();
	}
	
	//mark the project as non-modified one
	proj->SetModified(false);
}

void BuilderGnuMake::CreateMakeDirsTarget(BuildConfigPtr bldConf, const wxString &targetName, wxString &text)
{
	text << wxT("\n");
	text << targetName << wxT(":\n");

	if (wxGetOsVersion() & wxOS_WINDOWS) {
		text << wxT("\t@makedir \"") << bldConf->GetIntermediateDirectory() << wxT("\"\n");
	} else {
		//other OSs
		text << wxT("\t@test -d ") << bldConf->GetIntermediateDirectory() << wxT(" || mkdir ") << bldConf->GetIntermediateDirectory() << wxT("\n");
	}
	text << wxT("\n");
}

void BuilderGnuMake::CreateObjectList(ProjectPtr proj, wxString &text)
{
	std::vector<wxFileName> files;
	proj->GetFiles(files);
	text << wxT("Objects=");

	BuildConfigPtr bldConf = WorkspaceST::Get()->GetProjSelBuildConf(proj->GetName());
	int counter = 1;
	for (size_t i=0; i<files.size(); i++) {
		if ( !IsSource(files[i].GetExt())) {
			//test resource file
			if (wxGetOsVersion() & wxOS_WINDOWS) {
				if (!(IsResource(files[i].GetExt()) && bldConf && bldConf->IsResCompilerRequired())) {
					continue;
				}
			} else {
				continue;
			}
		}

		text << wxT("$(IntermediateDirectory)/") << files[i].GetName() << wxT("$(ObjectSuffix) ");
		if (counter % 10 == 0) {
			text << wxT("\\\n\t");
		}
		counter++;
	}
	text << wxT("\n\n");
}

void BuilderGnuMake::CreateFileTargets(ProjectPtr proj, wxString &text)
{
	//get the project specific build configuration for the workspace active
	//configuration
	BuildConfigPtr bldConf = WorkspaceST::Get()->GetProjSelBuildConf(proj->GetName());
	wxString cmpType = bldConf->GetCompilerType();
	//get the compiler settings
	CompilerPtr cmp = BuildSettingsConfigST::Get()->GetCompiler(cmpType);
	bool isGnu(false);
	if (	cmp->GetTool(wxT("CompilerName")).Contains(wxT("gcc")) ||
	        cmp->GetTool(wxT("CompilerName")).Contains(wxT("g++"))) {
		//for g++/gcc compilers, we use a special feature that allows automatic generation of the
		//objects along with their dependenices
		isGnu = true;
	}

	std::vector<wxFileName> files;
	proj->GetFiles(files);

	text << wxT("\n\n");
	//create rule per object
	text << wxT("##\n");
	text << wxT("## Objects\n");
	text << wxT("##\n");

	for (size_t i=0; i<files.size(); i++) {
		if (IsSource(files[i].GetExt()) ) {
			if (isGnu) {
				wxString objectName;
				objectName << wxT("$(IntermediateDirectory)/") << files[i].GetName() << wxT("$(ObjectSuffix)");

				wxString fileName   = files[i].GetFullPath(wxPATH_UNIX);
				wxString dependFile;
				dependFile << wxT("$(IntermediateDirectory)/") << files[i].GetName() << wxT("$(ObjectSuffix)") << wxT(".d");

				text << objectName << wxT(": ") << fileName << wxT(" ") << dependFile << wxT("\n");
				text << wxT("\t") << wxT("$(CompilerName) $(SourceSwitch)") << fileName << wxT(" $(CmpOptions)  ") << wxT(" $(ObjectSwitch)") << objectName << wxT(" $(IncludePath)\n");

				//add the dependencie rule
				text << dependFile << wxT(":") << wxT("\n");
				text << wxT("\t") << wxT("@$(CompilerName) $(CmpOptions) $(IncludePath) -MT") << objectName <<wxT(" -MF") << dependFile << wxT(" -MM ") << fileName << wxT("\n\n");

			} else {
				wxString objectName;
				objectName << wxT("$(IntermediateDirectory)/") << files[i].GetName() << wxT("$(ObjectSuffix)");

				wxString fileName   = files[i].GetFullPath(wxPATH_UNIX);
				text << objectName << wxT(": ") << fileName << wxT("\n");
				text << wxT("\t") << wxT("$(CompilerName) $(SourceSwitch)") << fileName << wxT(" $(CmpOptions)  ") << wxT(" $(ObjectSwitch)") << objectName << wxT(" $(IncludePath) \n\n");
			}
		} else if (IsResource(files.at(i).GetExt()) && bldConf->IsResCompilerRequired() && wxGetOsVersion() & wxOS_WINDOWS ) {
			//Windows only
			wxString objectName;
			objectName << wxT("$(IntermediateDirectory)/") << files[i].GetName() << wxT("$(ObjectSuffix)");

			wxString fileName   = files[i].GetFullPath(wxPATH_UNIX);
			text << objectName << wxT(": ") << fileName << wxT("\n");
			text << wxT("\t") << wxT("$(RcCompilerName) -i ") << fileName << wxT(" $(RcCmpOptions)  ") << wxT(" $(ObjectSwitch)") << objectName << wxT(" $(RcIncludePath) \n\n");
		}
	}

	//add clean target
	text << wxT("##\n");
	text << wxT("## Clean\n");
	text << wxT("##\n");
	text << wxT("clean:\n");

	if (wxGetOsVersion() & wxOS_WINDOWS) {
		//windows clean command
		for (size_t i=0; i<files.size(); i++) {
			if (IsSource(files[i].GetExt())) {
				wxString objectName = files[i].GetName() << wxT("$(ObjectSuffix)");
				wxString dependFile = files[i].GetName() << wxT("$(ObjectSuffix)") << wxT(".d");
				text << wxT("\t") << wxT("$(RM) ") << wxT("$(IntermediateDirectory)/") << objectName << wxT("\n");
				text << wxT("\t") << wxT("$(RM) ") << wxT("$(IntermediateDirectory)/") << dependFile << wxT("\n");
			} else if (IsResource(files[i].GetExt()) && bldConf->IsResCompilerRequired() && wxGetOsVersion() & wxOS_WINDOWS) {
				wxString ofile = files[i].GetName() << wxT("$(ObjectSuffix)");
				text << wxT("\t") << wxT("$(RM) ") << wxT("$(IntermediateDirectory)/") << ofile << wxT("\n");
			}
		}
		//delete the output file as well
		wxString exeExt(wxEmptyString);
		if (proj->GetSettings()->GetProjectType() == Project::EXECUTABLE) {
			//under windows, g++ automatically adds the .exe extension to executable
			//make sure we deletes it as well
			exeExt = wxT(".exe");
		}
		text << wxT("\t") << wxT("$(RM) ") << wxT("$(OutputFile)") << wxT("\n");;
		text << wxT("\t") << wxT("$(RM) ") << wxT("$(OutputFile)") << exeExt << wxT("\n");;
	} else {
		//on linux we dont really need resource compiler...
		for (size_t i=0; i<files.size(); i++) {
			if ( !IsSource(files[i].GetExt()) )
				continue;

			wxString objectName = files[i].GetName() << wxT("$(ObjectSuffix)");
			wxString dependFile = files[i].GetName() << wxT("$(ObjectSuffix)") << wxT(".d");

			text << wxT("\t") << wxT("$(RM) ") << wxT("$(IntermediateDirectory)/") << objectName << wxT("\n");
			text << wxT("\t") << wxT("$(RM) ") << wxT("$(IntermediateDirectory)/") << dependFile << wxT("\n");
		}
		//delete the output file as well
		text << wxT("\t") << wxT("$(RM) ") << wxT("$(OutputFile)\n");
	}

	if (isGnu) {
		text << wxT("\n-include $(IntermediateDirectory)/*.d\n");
	}
	text << wxT("\n");
}

void BuilderGnuMake::CreateTargets(const wxString &type, BuildConfigPtr bldConf, wxString &text)
{
	//create the main target
	wxString name = bldConf->GetName();
	name = NormalizeConfigName(name);

	if (type == Project::STATIC_LIBRARY) {
		//create a static library
		text << wxT("\t") << wxT("$(ArchiveTool) $(ArchiveOutputSwitch)$(OutputFile) $(Objects)\n");
	} else
		if (type == Project::DYNAMIC_LIBRARY) {
			//create a shared library
			text << wxT("\t") << wxT("$(SharedObjectLinkerName) $(OutputSwitch)$(OutputFile) $(Objects) $(LibPath) $(Libs) $(LinkOptions)\n");
		} else
			if (type == Project::EXECUTABLE) {
				//create an executable
				text << wxT("\t") << wxT("$(LinkerName) $(OutputSwitch)$(OutputFile) $(Objects) $(LibPath) $(Libs) $(LinkOptions)\n");
			}
}

void BuilderGnuMake::CreatePostBuildEvents(BuildConfigPtr bldConf, wxString &text)
{
	BuildCommandList cmds;
	BuildCommandList::iterator iter;
	wxString name = bldConf->GetName();
	name = NormalizeConfigName(name);

	//generate postbuild commands
	cmds.clear();
	bldConf->GetPostBuildCommands(cmds);
	bool first(true);
	if (!cmds.empty()) {
		iter = cmds.begin();
		for (; iter != cmds.end(); iter++) {
			if (iter->GetEnabled()) {
				if (first) {
					text << wxT("\t@echo Executing Post Build commands ...\n");
					first = false;
				}
				text << wxT("\t") << iter->GetCommand() << wxT("\n");
			}
		}
		if (!first) {
			text << wxT("\t@echo Done\n");
		}
	}
}

bool BuilderGnuMake::HasPrebuildCommands(BuildConfigPtr bldConf) const
{
	BuildCommandList cmds;
	BuildCommandList::const_iterator iter;
	bldConf->GetPreBuildCommands(cmds);
	bool first(true);

	if (!cmds.empty()) {
		iter = cmds.begin();
		for (; iter != cmds.end(); iter++) {
			if (iter->GetEnabled()) {
				if (first) {
					first = false;
					break;
				}
			}
		}
	}
	return !first;
}

void BuilderGnuMake::CreatePreBuildEvents(BuildConfigPtr bldConf, wxString &text)
{
	BuildCommandList cmds;
	BuildCommandList::iterator iter;
	wxString name = bldConf->GetName();
	name = NormalizeConfigName(name);

	//add PrePreBuild
	if (!bldConf->GetPreBuildCustom().IsEmpty()) {
		text << wxT("PrePreBuild: ");
		text << bldConf->GetPreBuildCustom() << wxT("\n");
	}
	text << wxT("\n");

	cmds.clear();
	bldConf->GetPreBuildCommands(cmds);
	bool first(true);
	text << wxT("PreBuild:\n");
	if (!cmds.empty()) {
		iter = cmds.begin();
		for (; iter != cmds.end(); iter++) {
			if (iter->GetEnabled()) {
				if (first) {
					text << wxT("\t@echo Executing Pre Build commands ...\n");
					first = false;
				}
				text << wxT("\t") << iter->GetCommand() << wxT("\n");
			}
		}
		if (!first) {
			text << wxT("\t@echo Done\n");
		}
	}
}

void BuilderGnuMake::CreateConfigsVariables(ProjectPtr proj, BuildConfigPtr bldConf, wxString &text)
{
	wxString name = bldConf->GetName();
	name = NormalizeConfigName(name);

	wxString cmpType = bldConf->GetCompilerType();
	CompilerPtr cmp = BuildSettingsConfigST::Get()->GetCompiler(cmpType);
	
	text << wxT("## ") << name << wxT("\n");
	text << wxT("ifeq ($(type),") << name << wxT(")") << wxT("\n");
	//The following two variables are here for compatibility with MSVS
	text << wxT("ConfigurationName :=") << name << wxT("\n");
	text << wxT("IntermediateDirectory :=") << bldConf->GetIntermediateDirectory() << wxT("\n");
	text << wxT("OutDir := $(IntermediateDirectory)\n");
	text << wxT("LinkerName:=") << cmp->GetTool(wxT("LinkerName")) << wxT("\n");
	text << wxT("ArchiveTool :=") << cmp->GetTool(wxT("ArchiveTool")) << wxT("\n");
	text << wxT("SharedObjectLinkerName :=") << cmp->GetTool(wxT("SharedObjectLinkerName")) << wxT("\n");
	text << wxT("ObjectSuffix :=") << cmp->GetObjectSuffix() << wxT("\n");
	text << wxT("DebugSwitch :=") << cmp->GetSwitch(wxT("Debug")) << wxT("\n");
	text << wxT("IncludeSwitch :=") << cmp->GetSwitch(wxT("Include")) << wxT("\n");
	text << wxT("LibrarySwitch :=") << cmp->GetSwitch(wxT("Library")) << wxT("\n");
	text << wxT("OutputSwitch :=") << cmp->GetSwitch(wxT("Output")) << wxT("\n");
	text << wxT("LibraryPathSwitch :=") << cmp->GetSwitch(wxT("LibraryPath")) << wxT("\n");
	text << wxT("PreprocessorSwitch :=") << cmp->GetSwitch(wxT("Preprocessor")) << wxT("\n");
	text << wxT("SourceSwitch :=") << cmp->GetSwitch(wxT("Source")) << wxT("\n");
	text << wxT("CompilerName :=") << cmp->GetTool(wxT("CompilerName")) << wxT("\n");
	
	text << wxT("OutputFile :=") << bldConf->GetOutputFileName() << wxT("\n");
	text << wxT("Preprocessors :=") << ParsePreprocessor(bldConf->GetPreprocessor()) << wxT("\n");
	text << wxT("ObjectSwitch :=") << cmp->GetSwitch(wxT("Object")) << wxT("\n");
	text << wxT("ArchiveOutputSwitch :=") << cmp->GetSwitch(wxT("ArchiveOutput")) << wxT("\n");
	
	wxString buildOpts = bldConf->GetCompileOptions();
	buildOpts.Replace(wxT(";"), wxT(" "));
	text << wxT("CmpOptions :=") << buildOpts << wxT(" $(Preprocessors)") << wxT("\n");

	
	//only if resource compiler required, evaluate the resource variables
	if(bldConf->IsResCompilerRequired()) {
		wxString rcBuildOpts = bldConf->GetResCompileOptions();
		rcBuildOpts.Replace(wxT(";"), wxT(" "));
		text << wxT("RcCmpOptions :=") << rcBuildOpts << wxT("\n");
		text << wxT("RcCompilerName :=") << cmp->GetTool(wxT("ResourceCompiler")) << wxT("\n");
	}
	
	wxString linkOpt = bldConf->GetLinkOptions();
	linkOpt.Replace(wxT(";"), wxT(" "));

	//link options are kept with semi-colons, strip them
	text << wxT("LinkOptions := ") << linkOpt << wxT("\n");
	
	// add the global include path followed by the project include path
	text << wxT("IncludePath := ") << ParseIncludePath(cmp->GetGlobalIncludePath(), proj->GetName()) << wxT(" ") << ParseIncludePath(bldConf->GetIncludePath(), proj->GetName()) << wxT("\n");
	
	text << wxT("RcIncludePath :=") << ParseIncludePath(bldConf->GetResCmpIncludePath(), proj->GetName()) << wxT("\n");
	text << wxT("Libs :=") << ParseLibs(bldConf->GetLibraries()) << wxT("\n");
	
	// add the global library path followed by the project library path
	text << wxT("LibPath :=") << ParseLibPath(cmp->GetGlobalLibPath(), proj->GetName()) << wxT(" ") << ParseLibPath(bldConf->GetLibPath(), proj->GetName()) << wxT("\n");
	text << wxT("endif\n\n");
}

wxString BuilderGnuMake::ParseIncludePath(const wxString &paths, const wxString &projectName)
{
	//convert semi-colon delimited string into GNU list of
	//include paths:
	wxString incluedPath(wxEmptyString);
	wxStringTokenizer tkz(paths, wxT(";"));
	//prepend each include path with -I
	while (tkz.HasMoreTokens()) {
		wxString path(tkz.NextToken());
		TrimString(path);
		path = ExpandAllVariables(path, projectName, wxEmptyString);
		path.Replace(wxT("\\"), wxT("/"));
		incluedPath << wxT("$(IncludeSwitch)") << path << wxT(" ");
	}
	return incluedPath;
}

wxString BuilderGnuMake::ParseLibPath(const wxString &paths, const wxString &projectName)
{
	//convert semi-colon delimited string into GNU list of
	//lib path
	wxString libPath(wxEmptyString);
	wxStringTokenizer tkz(paths, wxT(";"));
	//prepend each include path with libpath switch
	while (tkz.HasMoreTokens()) {
		wxString path(tkz.NextToken());
		TrimString(path);
		path = ExpandAllVariables(path, projectName, wxEmptyString);
		path.Replace(wxT("\\"), wxT("/"));
		libPath << wxT("$(LibraryPathSwitch)") << path << wxT(" ");
	}
	return libPath;
}

wxString BuilderGnuMake::ParsePreprocessor(const wxString &prep)
{
	wxString preprocessor(wxEmptyString);
	wxStringTokenizer tkz(prep, wxT(";"));
	//prepend each include path with libpath switch
	while (tkz.HasMoreTokens()) {
		wxString p(tkz.NextToken());
		TrimString(p);
		preprocessor << wxT("$(PreprocessorSwitch)") << p << wxT(" ");
	}
	return preprocessor;
}

wxString BuilderGnuMake::ParseLibs(const wxString &libs)
{
	//convert semi-colon delimited string into GNU list of
	//libs
	wxString slibs(wxEmptyString);
	wxStringTokenizer tkz(libs, wxT(";"));
	//prepend each include path with -l and strip trailing lib string
	//also, if the file contains an extension (.a, .so, .dynlib) remove them as well
	while (tkz.HasMoreTokens()) {
		wxString lib(tkz.NextToken());
		TrimString(lib);
		//remove lib prefix
		if (lib.StartsWith(wxT("lib"))) {
			lib = lib.Mid(3);
		}

		//remove known suffixes
		if(	lib.EndsWith(wxT(".a")) || 
			lib.EndsWith(wxT(".so")) || 
			lib.EndsWith(wxT(".dylib")) || 
			lib.EndsWith(wxT(".dll"))
		){
			lib = lib.BeforeLast(wxT('.'));
		} 
		
		slibs << wxT("$(LibrarySwitch)") << lib << wxT(" ");
	}
	return slibs;
}

wxString BuilderGnuMake::GetBuildCommand(const wxString &project, bool &isCustom)
{
	wxString errMsg, cmd;
	isCustom = false;
	BuildConfigPtr bldConf = WorkspaceST::Get()->GetProjSelBuildConf(project);
	if (!bldConf) {
		return wxEmptyString;
	}

	if( bldConf->IsCustomBuild() ) {
		//we got a custom build here, return the command as appears in the 
		//'custom build line'
		isCustom = true;
		return bldConf->GetCustomBuildCmd();
	}
	
	//generate the makefile
	Export(project, false, false, errMsg);
	
	BuildMatrixPtr matrix = WorkspaceST::Get()->GetBuildMatrix();
	wxString buildTool = BuildManagerST::Get()->GetSelectedBuilder()->GetBuildToolCommand(true);
	buildTool = WorkspaceST::Get()->ExpandVariables(buildTool);

	wxString type = Builder::NormalizeConfigName(matrix->GetSelectedConfigurationName());
	cmd << buildTool << wxT(" \"") << WorkspaceST::Get()->GetName() << wxT("_wsp.mk\" type=") << type;
	return cmd;
}

wxString BuilderGnuMake::GetCleanCommand(const wxString &project, bool &isCustom)
{
	wxString errMsg, cmd;
	isCustom = false;
	BuildConfigPtr bldConf = WorkspaceST::Get()->GetProjSelBuildConf(project);
	if (!bldConf) {
		return wxEmptyString;
	}
	
	if( bldConf->IsCustomBuild() ) {
		//we got a custom build here, return the command as appears in the 
		//'custom build line'
		isCustom = true;
		return bldConf->GetCustomCleanCmd();
	}

	//generate the makefile
	Export(project, false, false, errMsg);
	
	wxString buildTool = BuildManagerST::Get()->GetSelectedBuilder()->GetBuildToolCommand(true);
	buildTool = WorkspaceST::Get()->ExpandVariables(buildTool);

	BuildMatrixPtr matrix = WorkspaceST::Get()->GetBuildMatrix();
	wxString type = Builder::NormalizeConfigName(matrix->GetSelectedConfigurationName());
	cmd << buildTool << wxT(" \"") << WorkspaceST::Get()->GetName() << wxT("_wsp.mk\" type=") << type << wxT(" clean");
	return cmd;
}

wxString BuilderGnuMake::GetPOBuildCommand(const wxString &project, bool &isCustom)
{
	wxString errMsg, cmd;
	isCustom = false;
	BuildConfigPtr bldConf = WorkspaceST::Get()->GetProjSelBuildConf(project);
	if (!bldConf) {
		return wxEmptyString;
	}
	
	if( bldConf->IsCustomBuild() ) {
		//we got a custom build here, return the command as appears in the 
		//'custom build command'
		isCustom = true;
		return bldConf->GetCustomBuildCmd();
	}
	
	//generate the makefile
	Export(project, true, false, errMsg);
	
	BuildMatrixPtr matrix = WorkspaceST::Get()->GetBuildMatrix();
	wxString buildTool = BuildManagerST::Get()->GetSelectedBuilder()->GetBuildToolCommand(true);
	buildTool = WorkspaceST::Get()->ExpandVariables(buildTool);

	wxString type = matrix->GetProjectSelectedConf(matrix->GetSelectedConfigurationName(), project);

	//cd to the project directory
	cmd << buildTool << wxT(" \"") << project << wxT(".mk\" type=") << type;
	return cmd;
}

wxString BuilderGnuMake::GetPOCleanCommand(const wxString &project, bool &isCustom)
{
	wxString errMsg, cmd;
	isCustom = false;
	BuildConfigPtr bldConf = WorkspaceST::Get()->GetProjSelBuildConf(project);
	if (!bldConf) {
		return wxEmptyString;
	}

	if( bldConf->IsCustomBuild() ) {
		//we got a custom build here, return the command as appears in the 
		//'custom build clean command'
		isCustom = true;
		return bldConf->GetCustomCleanCmd();
	}
	
	//generate the makefile
	Export(project, true, false, errMsg);
	
	BuildMatrixPtr matrix = WorkspaceST::Get()->GetBuildMatrix();
	wxString buildTool = BuildManagerST::Get()->GetSelectedBuilder()->GetBuildToolCommand(true);
	buildTool = WorkspaceST::Get()->ExpandVariables(buildTool);

	wxString type = matrix->GetProjectSelectedConf(matrix->GetSelectedConfigurationName(), project);

	//cd to the project directory
	cmd << buildTool << wxT(" \"") << project << wxT(".mk\" type=") << type << wxT(" clean");
	return cmd;
}

wxString BuilderGnuMake::GetSingleFileCmd(const wxString &project, const wxString &fileName, bool &isCustom, wxString &errMsg)
{
	wxString cmd;
	BuildConfigPtr bldConf = WorkspaceST::Get()->GetProjSelBuildConf(project);
	if (!bldConf) {
		return wxEmptyString;
	}
	
	if(bldConf->IsCustomBuild()) {
		isCustom = true;
		return bldConf->GetSingleFileBuildCommand();
	}
	
	//generate the makefile
	Export(project, true, false, errMsg);

	BuildMatrixPtr matrix = WorkspaceST::Get()->GetBuildMatrix();
	wxString buildTool = BuildManagerST::Get()->GetSelectedBuilder()->GetBuildToolCommand(true);
	wxString type = matrix->GetProjectSelectedConf(matrix->GetSelectedConfigurationName(), project);

	//create the target 
	wxString tareget;
	wxString objSuffix;
	wxFileName fn(fileName);
	
	wxString cmpType = bldConf->GetCompilerType();
	CompilerPtr cmp = BuildSettingsConfigST::Get()->GetCompiler(cmpType);
	
	tareget << bldConf->GetIntermediateDirectory() << wxT("/") << fn.GetName() << cmp->GetObjectSuffix();
	cmd << buildTool << wxT(" \"") << project << wxT(".mk\" type=") << type << wxT(" ") << tareget;
	
	return EnvironmentConfig::Instance()->ExpandVariables(cmd);
}

wxString BuilderGnuMake::GetCdCmd(const wxFileName &path1, const wxFileName &path2)
{
	wxString cd_cmd(wxT("@"));
	if (path2.GetPath().IsEmpty()) {
		return cd_cmd;
	}

	if (path1.GetPath() != path2.GetPath()) {
		cd_cmd << wxT("cd \"") << path2.GetPath() << wxT("\" && ");
	}
	return cd_cmd;
}
