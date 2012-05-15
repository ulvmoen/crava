#include <iostream>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <limits.h>
#define _USE_MATH_DEFINES
#include <cmath>

#include "src/definitions.h"
#include "src/modelgeneral.h"
#include "src/xmlmodelfile.h"
#include "src/modelsettings.h"
#include "src/wavelet1D.h"
#include "src/wavelet3D.h"
#include "src/corr.h"
#include "src/analyzelog.h"
#include "src/vario.h"
#include "src/simbox.h"
#include "src/background.h"
#include "src/welldata.h"
#include "src/blockedlogs.h"
#include "src/fftgrid.h"
#include "src/fftfilegrid.h"
#include "src/gridmapping.h"
#include "src/inputfiles.h"
#include "src/timings.h"
#include "src/io.h"
#include "src/waveletfilter.h"
#include "src/tasklist.h"
#include "src/timeline.h"
#include "src/state4d.h"
#include "src/modelavodynamic.h"

#include "lib/utils.h"
#include "lib/random.h"
#include "lib/timekit.hpp"
#include "lib/lib_matr.h"
#include "nrlib/iotools/fileio.hpp"
#include "nrlib/iotools/stringtools.hpp"
#include "nrlib/segy/segy.hpp"
#include "nrlib/surface/surfaceio.hpp"
#include "nrlib/surface/surface.hpp"
#include "nrlib/surface/regularsurface.hpp"
#include "nrlib/iotools/logkit.hpp"
#include "nrlib/stormgrid/stormcontgrid.hpp"
#include "rplib/rockphysicsstorage.h"

ModelGeneral::ModelGeneral(ModelSettings *& modelSettings, const InputFiles * inputFiles, Simbox *& timeBGSimbox)
{
  timeSimbox_             = new Simbox();
  timeSimboxConstThick_   = NULL;

  correlationDirection_   = NULL;
  randomGen_              = NULL;
  failed_                 = false;
  gradX_                  = 0.0;
  gradY_                  = 0.0;

  timeDepthMapping_       = NULL;
  timeCutMapping_         = NULL;
  velocityFromInversion_  = false;

  bool failedSimbox       = false;
  bool failedDepthConv    = false;
  bool failedRockPhysics  = false;

  bool failedLoadingModel = false;

  bool failedWells        = false;

  trendCubes_             = NULL;

  Simbox * timeCutSimbox  = NULL;
  timeLine_               = NULL;

  forwardModeling_        = modelSettings->getForwardModeling();
  numberOfWells_          = modelSettings->getNumberOfWells();


  {
    int debugLevel = modelSettings->getLogLevel();
    if(modelSettings->getDebugLevel() == 1)
      debugLevel = LogKit::L_DebugLow;
    else if(modelSettings->getDebugLevel() == 2)
      debugLevel = LogKit::L_DebugHigh;

    LogKit::SetScreenLog(debugLevel);

    std::string logFileName = IO::makeFullFileName("",IO::FileLog()+IO::SuffixTextFiles());
    LogKit::SetFileLog(logFileName,modelSettings->getLogLevel());

    if(modelSettings->getDebugFlag() > 0)
    {
      std::string fName = IO::makeFullFileName("",IO::FileDebug()+IO::SuffixTextFiles());
      LogKit::SetFileLog(fName, debugLevel);
    }

    if(modelSettings->getErrorFileFlag() == true)
    {
      std::string fName = IO::makeFullFileName("",IO::FileError()+IO::SuffixTextFiles());
      LogKit::SetFileLog(fName, LogKit::Error);
    }
    LogKit::EndBuffering();

    if(inputFiles->getSeedFile() == "")
      randomGen_ = new RandomGen(modelSettings->getSeed());
    else
      randomGen_ = new RandomGen(inputFiles->getSeedFile().c_str());

    if(modelSettings->getNumberOfSimulations() == 0)
      modelSettings->setWritePrediction(true); //write predicted grids.

    printSettings(modelSettings, inputFiles);

    //Set output for all FFTGrids.
    FFTGrid::setOutputFlags(modelSettings->getOutputGridFormat(),
                            modelSettings->getOutputGridDomain());

    std::string errText("");

    LogKit::WriteHeader("Defining modelling grid");
    makeTimeSimboxes(timeSimbox_, timeCutSimbox, timeBGSimbox, timeSimboxConstThick_,  //Handles correlation direction too.
                     correlationDirection_, modelSettings, inputFiles,
                     errText, failedSimbox);

    if(!failedSimbox)
    {
      //
      // FORWARD MODELLING
      //
      if (modelSettings->getForwardModeling() == true)
      {
  //      checkAvailableMemory(timeSimbox_, modelSettings, inputFiles);
      }
      else {
        //
        // INVERSION/ESTIMATION
        //
        if(timeCutSimbox!=NULL)  {
          timeCutMapping_ = new GridMapping();
          timeCutMapping_->makeTimeTimeMapping(timeCutSimbox);
        }

        //checkAvailableMemory(timeSimbox_, modelSettings, inputFiles);

        bool estimationMode = modelSettings->getEstimationMode();

        if(estimationMode == false && modelSettings->getDoDepthConversion() == true)
        {
          processDepthConversion(timeCutSimbox, timeSimbox_, modelSettings,
                                 inputFiles, errText, failedDepthConv);
        }

        if(failedDepthConv == false)
          processRockPhysics(timeSimbox_, timeCutSimbox, modelSettings, failedRockPhysics, errText, inputFiles);

        //Set up timeline.
        timeLine_ = new TimeLine();
        //Activate below when gravity data are ready.
        //Do gravity first.
        //for(int i=0;i<modelSettings->getNumberOfGravityData();i++) {
        //  int time = computeTime(modelSettings->getGravityYear[i],
        //                         modelSettings->getGravityMonth[i],
        //                         modelSettings->getGravityDay[i]);
        //  timeLine_->AddEvent(time, TimeLine::GRAVITY, i);

        for(int i=0;i<modelSettings->getNumberOfVintages();i++) {
          //Vintages may have both travel time and AVO
          int time = computeTime(modelSettings->getVintageYear(i),
                                 modelSettings->getVintageMonth(i),
                                 modelSettings->getVintageDay(i));
          //Activate below when travel time is ready.
          //Travel time ebefore AVO for same vintage.
          //if(travel time for this vintage)
          //timeLine_->AddEvent(time, TimeLine::TRAVEL_TIME, i);
          if(modelSettings->getNumberOfAngles(i) > 0) //Check for AVO data, could be pure travel time.
            timeLine_->AddEvent(time, TimeLine::AVO, i);
        }
        processWells(wells_, timeSimbox_, modelSettings, inputFiles, errText, failedWells);
      }
    }
    failedLoadingModel = failedSimbox  || failedDepthConv || failedWells;

    if (failedLoadingModel) {
      LogKit::WriteHeader("Error(s) while loading data");
      LogKit::LogFormatted(LogKit::Error,"\n"+errText);
      LogKit::LogFormatted(LogKit::Error,"\nAborting\n");
    }
  }

  failed_ = failedLoadingModel;
  failed_details_.push_back(failedSimbox);
  failed_details_.push_back(failedDepthConv);
  failed_details_.push_back(failedWells);

  if(timeCutSimbox != NULL)
    delete timeCutSimbox;
}


ModelGeneral::~ModelGeneral(void)
{
  if(timeDepthMapping_!=NULL)
    delete timeDepthMapping_;

  if(timeCutMapping_!=NULL)
    delete timeCutMapping_;

  if(correlationDirection_ !=NULL)
    delete correlationDirection_;

  if(trendCubes_ != NULL){
    for(int i=0; i<numberOfTrendCubes_; i++)
      delete trendCubes_[i];
    delete [] trendCubes_;
  }

  for(int i=0; i<static_cast<int>(rock_distributions_.size()); i++)
    delete rock_distributions_[i];

  delete randomGen_;
  delete timeSimbox_;
  delete timeSimboxConstThick_;

   if(!forwardModeling_)
  {
    for(int i=0 ; i<numberOfWells_ ; i++)
      if(wells_[i] != NULL)
        delete wells_[i];
  }

}


void
ModelGeneral::readSegyFile(const std::string       & fileName,
                           FFTGrid                *& target,
                           Simbox                  * timeSimbox,
                           Simbox                  * timeCutSimbox,
                           ModelSettings          *& modelSettings,
                           const SegyGeometry     *& geometry,
                           int                       gridType,
                           float                     offset,
                           const TraceHeaderFormat * format,
                           std::string             & errText,
                           bool                      nopadding)
{
  SegY * segy = NULL;
  bool failed = false;
  target = NULL;

  try
  {
    //
    // Currently we have only one optional TraceHeaderFormat, but this can
    // be augmented to a list with several formats ...
    //
    if(format == NULL) { //Unknown format
      std::vector<TraceHeaderFormat*> traceHeaderFormats(0);
      if (modelSettings->getTraceHeaderFormat() != NULL)
      {
        traceHeaderFormats.push_back(modelSettings->getTraceHeaderFormat());
      }
      segy = new SegY(fileName,
                      offset,
                      traceHeaderFormats,
                      true); // Add standard formats to format search
    }
    else //Known format, read directly.
      segy = new SegY(fileName, offset, *format);

    float guard_zone = modelSettings->getGuardZone();

    std::string errTxt = "";
    checkThatDataCoverGrid(segy,
                           offset,
                           timeCutSimbox,
                           guard_zone,
                           errTxt);

    if (errTxt == "") {
      bool  onlyVolume      = true;
      float padding        = 2*guard_zone; // This is *not* the same as FFT-grid padding
      bool  relativePadding = false;

      segy->ReadAllTraces(timeCutSimbox,
                          padding,
                          onlyVolume,
                          relativePadding);
      segy->CreateRegularGrid();
    }
    else {
      errText += errTxt;
      failed = true;
    }
  }
  catch (NRLib::Exception & e)
  {
    errText += e.what();
    failed = true;
  }

  if (!failed)
  {
    int missingTracesSimbox  = 0;
    int missingTracesPadding = 0;
    int deadTracesSimbox     = 0;

    const SegyGeometry * geo;
    geo = segy->GetGeometry();
    geo->WriteGeometry();
    if (gridType == FFTGrid::DATA)
      geometry = new SegyGeometry(geo);

    int xpad, ypad, zpad;
    if(nopadding)
    {
      xpad = timeSimbox->getnx();
      ypad = timeSimbox->getny();
      zpad = timeSimbox->getnz();
    }
    else
    {
      xpad = modelSettings->getNXpad();
      ypad = modelSettings->getNYpad();
      zpad = modelSettings->getNZpad();
    }
    target = createFFTGrid(timeSimbox->getnx(),
                           timeSimbox->getny(),
                           timeSimbox->getnz(),
                           xpad,
                           ypad,
                           zpad,
                           modelSettings->getFileGrid());
    target->setType(gridType);

    if (gridType == FFTGrid::DATA) {
      target->fillInSeismicDataFromSegY(segy,
                                        timeSimbox,
                                        timeCutSimbox,
                                        modelSettings->getSmoothLength(),
                                        missingTracesSimbox,
                                        missingTracesPadding,
                                        deadTracesSimbox,
                                        errText);
    }
    else {
      missingTracesSimbox = target->fillInFromSegY(segy,
                                                   timeSimbox,
                                                   nopadding);
    }

    if (missingTracesSimbox > 0) {
      if(missingTracesSimbox == timeSimbox->getnx()*timeSimbox->getny()) {
        errText += "Error: Data in file "+fileName+" was completely outside the inversion area.\n";
        failed = true;
      }
      else {
        if(gridType == FFTGrid::PARAMETER) {
          errText += "Grid in file "+fileName+" does not cover the inversion area.\n";
        }
        else {
          LogKit::LogMessage(LogKit::Warning, "Warning: "+NRLib::ToString(missingTracesSimbox)
                             +" grid columns were outside the seismic data area.");
          std::string text;
          text += "Check seismic volumes and inversion area: A part of the inversion area is outside\n";
          text += "    the seismic data specified in file \'"+fileName+"\'.";
          TaskList::addTask(text);
        }
      }
    }
    if (missingTracesPadding > 0) {
      int nxpad  = xpad - timeSimbox->getnx();
      int nypad  = ypad - timeSimbox->getny();
      int nxypad = nxpad*nypad;
      LogKit::LogMessage(LogKit::High, "Number of grid columns in padding with no seismic data: "
                         +NRLib::ToString(missingTracesPadding)+" of "+NRLib::ToString(nxypad)+"\n");
    }
    if (deadTracesSimbox > 0) {
      LogKit::LogMessage(LogKit::High, "Number of grid columns in grid with no seismic data   : "
                         +NRLib::ToString(missingTracesPadding)+" of "+NRLib::ToString(timeSimbox->getnx()*timeSimbox->getny())+"\n");
    }
  }
  if (segy != NULL)
    delete segy;
}


void
ModelGeneral::checkThatDataCoverGrid(SegY        * segy,
                                     float         offset,
                                     Simbox      * timeCutSimbox,
                                     float         guard_zone,
                                     std::string & errText)
{
  // Seismic data coverage (translate to CRAVA grid by adding half a grid cell)
  float dz = segy->GetDz();
  float z0 = offset + 0.5f*dz;
  float zn = z0 + (segy->GetNz() - 1)*dz;

  // Top and base of interval of interest
  float top_grid = timeCutSimbox->getTopZMin();
  float bot_grid = timeCutSimbox->getBotZMax();

  // Find guard zone
  float top_guard = top_grid - guard_zone;
  float bot_guard = bot_grid + guard_zone;

  if (top_guard < z0) {
    float z0_new = z0 - ceil((z0 - top_guard)/dz)*dz;
    errText += "\nThere is not enough seismic data above the interval of interest. The seismic data\n";
    errText += "must start at "+NRLib::ToString(z0_new)+"ms (in CRAVA grid) to allow for a ";
    errText += NRLib::ToString(guard_zone)+"ms FFT guard zone:\n\n";
    errText += "  Seismic data start           : "+NRLib::ToString(z0,1)+"  (in CRAVA grid)\n";
    errText += "  Seismic data end             : "+NRLib::ToString(zn,1)+"  (in CRAVA grid)\n\n";
    errText += "  Top of interval-of-interest  : "+NRLib::ToString(top_grid,1)+"\n";
    errText += "  Base of interval-of-interest : "+NRLib::ToString(bot_grid,1)+"\n\n";
    errText += "  Top of upper guard zone      : "+NRLib::ToString(top_guard,1)+"\n";
    errText += "  Base of lower guard zone     : "+NRLib::ToString(bot_guard,1)+"\n";
  }
  if (bot_guard > zn) {
    float zn_new = zn + ceil((bot_guard - zn)/dz)*dz;
    errText += "\nThere is not enough seismic data below the interval of interest. The seismic data\n";
    errText += "must end at "+NRLib::ToString(zn_new)+"ms (in CRAVA grid) to allow for a ";
    errText += NRLib::ToString(guard_zone)+"ms FFT guard zone:\n\n";
    errText += "  Seismic data start           : "+NRLib::ToString(z0,1)+"  (in CRAVA grid)\n";
    errText += "  Seismic data end             : "+NRLib::ToString(zn,1)+"  (in CRAVA grid)\n\n";
    errText += "  Top of interval-of-interest  : "+NRLib::ToString(top_grid,1)+"\n";
    errText += "  Base of interval-of-interest : "+NRLib::ToString(bot_grid,1)+"\n\n";
    errText += "  Top of upper guard zone      : "+NRLib::ToString(top_guard,1)+"\n";
    errText += "  Base of lower guard zone     : "+NRLib::ToString(bot_guard,1)+"\n";
  }
}


void
ModelGeneral::readStormFile(const std::string  & fName,
                            FFTGrid           *& target,
                            const int            gridType,
                            const std::string  & parName,
                            Simbox             * timeSimbox,
                            ModelSettings     *& modelSettings,
                            std::string        & errText,
                            bool                 scale,
                            bool                 nopadding)
{
  StormContGrid * stormgrid = NULL;
  bool failed = false;

  try
  {
    stormgrid = new StormContGrid(0,0,0);
    stormgrid->ReadFromFile(fName);
  }
  catch (NRLib::Exception & e)
  {
    errText += e.what();
    failed = true;
  }
  int xpad, ypad, zpad;
  if(nopadding==false)
  {
    xpad = modelSettings->getNXpad();
    ypad = modelSettings->getNYpad();
    zpad = modelSettings->getNZpad();
  }
  else
  {
    xpad = timeSimbox->getnx();
    ypad = timeSimbox->getny();
    zpad = timeSimbox->getnz();
  }

  int outsideTraces = 0;
  if(failed == false)
  {
    target = createFFTGrid(timeSimbox->getnx(),
                           timeSimbox->getny(),
                           timeSimbox->getnz(),
                           xpad,
                           ypad,
                           zpad,
                           modelSettings->getFileGrid());
    target->setType(gridType);
    outsideTraces = target->fillInFromStorm(timeSimbox,stormgrid, parName, scale, nopadding);
  }

  if (stormgrid != NULL)
    delete stormgrid;

  if(outsideTraces > 0) {
    if(outsideTraces == timeSimbox->getnx()*timeSimbox->getny()) {
      errText += "Error: Data in file \'"+fName+"\' was completely outside the inversion area.\n";
      failed = true;
    }
    else {
      if(gridType == FFTGrid::PARAMETER) {
        errText += "Error: Data read from file \'"+fName+"\' does not cover the inversion area.\n";
      }
      else {
        LogKit::LogMessage(LogKit::Warning, "Warning: "+NRLib::ToString(outsideTraces)
                           + " grid columns were outside the seismic data in file \'"+fName+"\'.\n");
        TaskList::addTask("Check seismic data and inversion area: One or volumes did not have data enough to cover entire grid.\n");
     }
    }
  }
}

int
ModelGeneral::setPaddingSize(int nx, double px)
{
  int leastint    = static_cast<int>(ceil(nx*(1.0f+px)));
  int closestprod = FFTGrid::findClosestFactorableNumber(leastint);
  return(closestprod);
}

void
ModelGeneral::makeTimeSimboxes(Simbox   *& timeSimbox,
                        Simbox          *& timeCutSimbox,
                        Simbox          *& timeBGSimbox,
                        Simbox          *& timeSimboxConstThick,
                        Surface         *& correlationDirection,
                        ModelSettings   *& modelSettings,
                        const InputFiles * inputFiles,
                        std::string      & errText,
                        bool             & failed)
{
  std::string gridFile("");

  int  areaSpecification      = modelSettings->getAreaSpecification();

  bool estimationModeNeedILXL = modelSettings->getEstimationMode() &&
                                (areaSpecification == ModelSettings::AREA_FROM_GRID_DATA ||
                                 areaSpecification == ModelSettings::AREA_FROM_GRID_DATA_AND_UTM ||
                                 areaSpecification == ModelSettings::AREA_FROM_GRID_DATA_AND_SURFACE ||
                                (modelSettings->getOutputGridsSeismic() & IO::ORIGINAL_SEISMIC_DATA) > 0 ||
                                (modelSettings->getOutputGridFormat() & IO::SEGY) > 0);

  if(modelSettings->getForwardModeling())
    gridFile = inputFiles->getBackFile(0);    // Get geometry from earth model (Vp)
  else {
    if (modelSettings->getEstimationMode() == false || estimationModeNeedILXL)
      gridFile = inputFiles->getSeismicFile(0,0); // Get area from first seismic data volume
  }
  SegyGeometry * ILXLGeometry = NULL; //Geometry with correct XL and IL settings.

  //
  // Set area geometry information
  // -----------------------------
  //
  std::string areaType;
  if (areaSpecification == ModelSettings::AREA_FROM_UTM)
  {
    // The geometry is already present in modelSettings (read from model file).
    LogKit::LogFormatted(LogKit::High,"\nArea information has been taken from model file\n");
    areaType = "Model file";
  }
  else if(areaSpecification == ModelSettings::AREA_FROM_SURFACE)
  {
    LogKit::LogFormatted(LogKit::High,"\nFinding area information from surface \'"+inputFiles->getAreaSurfaceFile()+"\'\n");
    areaType = "Surface";
    Surface surf(inputFiles->getAreaSurfaceFile());
    SegyGeometry geometry(surf);
    modelSettings->setAreaParameters(&geometry);
  }
  else if(areaSpecification == ModelSettings::AREA_FROM_GRID_DATA         ||
          areaSpecification == ModelSettings::AREA_FROM_GRID_DATA_AND_UTM ||
          areaSpecification == ModelSettings::AREA_FROM_GRID_DATA_AND_SURFACE)
  {
    LogKit::LogFormatted(LogKit::High,"\nFinding inversion area from grid data in file \'"+gridFile+"\'\n");
    areaType = "Grid data";
    std::string tmpErrText;
    SegyGeometry * geometry;
    getGeometryFromGridOnFile(gridFile,
                              modelSettings->getTraceHeaderFormat(0,0), //Trace header format is the same for all time lapses
                              geometry,
                              tmpErrText);

    modelSettings->setSeismicDataAreaParameters(geometry);
    if(geometry != NULL) {
      geometry->WriteGeometry();

      if (modelSettings->getAreaILXL().size() > 0 || modelSettings->getSnapGridToSeismicData()) {
        SegyGeometry * fullGeometry = geometry;

        std::vector<int> areaILXL;
        bool gotArea = true;

        //
        // Geometry is given as XY, but we snap it to IL and XL.
        //
        if (modelSettings->getSnapGridToSeismicData()) {
          SegyGeometry * templateGeometry = NULL;
          if (areaSpecification == ModelSettings::AREA_FROM_GRID_DATA_AND_UTM) {
            templateGeometry = modelSettings->getAreaParameters();
          }
          else if (areaSpecification == ModelSettings::AREA_FROM_GRID_DATA_AND_SURFACE) {
            Surface surf(inputFiles->getAreaSurfaceFile());
            templateGeometry = new SegyGeometry(surf);
          }
          else {
            errText += "CRAVA has been asked to identify a proper ILXL inversion area based\n";
            errText += "on XY input information, but no UTM coordinates or surface have\n";
            errText += "been specified in model file.\n";
            gotArea = false;
          }
          if (gotArea) {
            areaILXL = fullGeometry->findAreaILXL(templateGeometry);
          }
        }
        else {
          areaILXL = modelSettings->getAreaILXL();
        }

        if (gotArea) {
          try {
            bool interpolated, aligned;
            geometry = fullGeometry->GetILXLSubGeometry(areaILXL, interpolated, aligned);

            std::string text;
            if(interpolated == true) {
              if(aligned == true) {
                text  = "Check IL/XL specification: Specified IL- or XL-step is not an integer multiple\n";
                text += "   of those found in the seismic data. Furthermore, the distance between first\n";
                text += "   and last XL and/or IL does not match the step size.\n";
                TaskList::addTask(text);
              }
              else {
                text  = "Check IL/XL specification: Specified IL- or XL-step is not an integer multiple\n";
                text  = "   of those found in the seismic data.\n";
                TaskList::addTask(text);
              }
            }
            else if(aligned == true) {
              text  = "Check IL/XL specification: Either start or end of IL and/or XL interval does not\n";
              text += "   align with IL/XL in seismic data, or end IL and/or XL is not an integer multiple\n";
              text += "   of steps away from the start.\n";
              TaskList::addTask(text);
            }
          }
          catch (NRLib::Exception & e) {
            errText += "Error: "+std::string(e.what());
            geometry->WriteILXL(true);
            geometry = NULL;
            failed = true;
          }
        }
        delete fullGeometry;
      }
      else {
        geometry->WriteILXL();
      }
      if(!failed) {
        modelSettings->setAreaParameters(geometry);
        ILXLGeometry = geometry;
      }
    }
    else {
      errText += tmpErrText;
      failed = true;
    }
  }
  if(!failed)
  {
    const SegyGeometry * areaParams = modelSettings->getAreaParameters();
    failed = timeSimbox->setArea(areaParams, errText);

    if(failed)
    {
      writeAreas(areaParams,timeSimbox,areaType);
      errText += "The specified AREA extends outside the surface(s).\n";
    }
    else
    {
      LogKit::LogFormatted(LogKit::Low,"\nResolution                x0           y0            lx         ly     azimuth         dx      dy\n");
      LogKit::LogFormatted(LogKit::Low,"-------------------------------------------------------------------------------------------------\n");
      double azimuth = (-1)*timeSimbox->getAngle()*(180.0/M_PI);
      if (azimuth < 0)
        azimuth += 360.0;
      LogKit::LogFormatted(LogKit::Low,"%-12s     %11.2f  %11.2f    %10.2f %10.2f    %8.3f    %7.2f %7.2f\n",
                           areaType.c_str(),
                           timeSimbox->getx0(), timeSimbox->gety0(),
                           timeSimbox->getlx(), timeSimbox->getly(), azimuth,
                           timeSimbox->getdx(), timeSimbox->getdy());
    }

    float minHorRes = modelSettings->getMinHorizontalRes();
    if (timeSimbox->getdx() < minHorRes || timeSimbox->getdy() < minHorRes){
      failed = true;
      errText += "The horizontal resolution in dx and dy should normally be above "+NRLib::ToString(minHorRes)
        +" m. If you need a denser\n sampling, please specify a new <advanced-settings><minimum-horizontal-resolution>\n";
    }

    if(!failed)
    {
      //
      // Set IL/XL information in geometry
      // ---------------------------------
      //
      // Skip for estimation mode if possible:
      //   a) For speed
      //   b) Grid data may not be available.
      if (modelSettings->getEstimationMode() == false || estimationModeNeedILXL == true) {
        if(ILXLGeometry == NULL) {
          int gridType = IO::findGridType(gridFile);
          bool ilxl_info_available = ((gridType == IO::SEGY) || (gridType == IO::CRAVA));
          if (ilxl_info_available) {
            LogKit::LogFormatted(LogKit::High,"\nFinding IL/XL information from grid data file \'"+gridFile+"\'\n");
            std::string tmpErrText;
            getGeometryFromGridOnFile(gridFile,
                                      modelSettings->getTraceHeaderFormat(0,0), //Trace header format is the same for all time lapses
                                      ILXLGeometry,
                                      tmpErrText);
            if(ILXLGeometry == NULL) {
              errText += tmpErrText;
              failed = true;
            }
          }
          else {
            LogKit::LogFormatted(LogKit::High,"\nCannot extract IL/XL information from non-SEGY grid data file \'"+gridFile+"\'\n");
          }
        }
        if(ILXLGeometry != NULL) {
          if(timeSimbox->isAligned(ILXLGeometry))
            timeSimbox->setILXL(ILXLGeometry);
          delete ILXLGeometry;
        }
      }

      // Rotate variograms relative to simbox
      modelSettings->rotateVariograms(static_cast<float> (timeSimbox_->getAngle()));

      //
      // Set SURFACES
      //

      setSimboxSurfaces(timeSimbox,
                        inputFiles->getTimeSurfFiles(),
                        modelSettings,
                        errText,
                        failed);

      if(!failed)
      {
        if(modelSettings->getUseLocalWavelet() && timeSimbox->getIsConstantThick())
        {
          LogKit::LogFormatted(LogKit::Warning,"\nWarning: LOCALWAVELET is ignored when using constant thickness in DEPTH.\n");
          TaskList::addTask("If local wavelet is to be used, constant thickness in depth should be removed.");
        }


        int status = timeSimbox->calculateDz(modelSettings->getLzLimit(),errText);
        estimateZPaddingSize(timeSimbox, modelSettings);

        float minSampDens = modelSettings->getMinSamplingDensity();
        if (timeSimbox->getdz()*timeSimbox->getMinRelThick() < minSampDens){
          failed   = true;
          errText += "We normally discourage denser sampling than "+NRLib::ToString(minSampDens);
          errText += "ms in the time grid. If you really need\nthis, please use ";
          errText += "<project-settings><advanced-settings><minimum-sampling-density>\n";
        }

        if(status == Simbox::BOXOK)
        {
          logIntervalInformation(timeSimbox, "Time output interval:","Two-way-time");
          //
          // Make extended time simbox
          //
          if(inputFiles->getCorrDirFile() != "") {
            //
            // Get correlation direction
            //
            try {
              Surface tmpSurf(inputFiles->getCorrDirFile());
              if(timeSimbox->CheckSurface(tmpSurf) == true)
                correlationDirection = new Surface(tmpSurf);
              else {
                errText += "Error: Correlation surface does not cover volume.\n";
                failed = true;
              }
            }
            catch (NRLib::Exception & e) {
              errText += e.what();
              failed = true;
            }

            if(failed == false && modelSettings->getForwardModeling() == false) {
              //Extends timeSimbox for correlation coverage. Original stored in timeCutSimbox
              setupExtendedTimeSimbox(timeSimbox, correlationDirection,
                                      timeCutSimbox,
                                      modelSettings->getOutputGridFormat(),
                                      modelSettings->getOutputGridDomain(),
                                      modelSettings->getOtherOutputFlag());
            }

            estimateZPaddingSize(timeSimbox, modelSettings);

            status = timeSimbox->calculateDz(modelSettings->getLzLimit(),errText);

            if(status == Simbox::BOXOK)
              logIntervalInformation(timeSimbox, "Time inversion interval (extended relative to output interval due to correlation):","Two-way-time");
            else
            {
              errText += "Could not make the time simulation grid.\n";
              failed = true;
            }

            if(modelSettings->getForwardModeling() == false && failed == false) {
              setupExtendedBackgroundSimbox(timeSimbox, correlationDirection, timeBGSimbox,
                                            modelSettings->getOutputGridFormat(),
                                            modelSettings->getOutputGridDomain(),
                                            modelSettings->getOtherOutputFlag());
              status = timeBGSimbox->calculateDz(modelSettings->getLzLimit(),errText);
              if(status == Simbox::BOXOK)
                logIntervalInformation(timeBGSimbox, "Time interval used for background modelling:","Two-way-time");
              else
              {
                errText += "Could not make the grid for background model.\n";
                failed = true;
              }
            }
          }

          if(failed == false) {
            estimateXYPaddingSizes(timeSimbox, modelSettings);

            unsigned long long int gridsize = static_cast<unsigned long long int>(modelSettings->getNXpad())*modelSettings->getNYpad()*modelSettings->getNZpad();

            if(gridsize > std::numeric_limits<unsigned int>::max()) {
              float fsize = 4.0f*static_cast<float>(gridsize)/static_cast<float>(1024*1024*1024);
              float fmax  = 4.0f*static_cast<float>(std::numeric_limits<unsigned int>::max()/static_cast<float>(1024*1024*1024));
              errText += "Grids as large as "+NRLib::ToString(fsize,1)+"GB cannot be handled. The largest accepted grid size\n";
              errText += "is "+NRLib::ToString(fmax)+"GB. Please reduce the number of layers or the lateral resolution.\n";
              failed = true;
            }

            LogKit::LogFormatted(LogKit::Low,"\nTime simulation grids:\n");
            LogKit::LogFormatted(LogKit::Low,"  Output grid         %4i * %4i * %4i   : %10llu\n",
                                 timeSimbox->getnx(),timeSimbox->getny(),timeSimbox->getnz(),
                                 static_cast<unsigned long long int>(timeSimbox->getnx())*timeSimbox->getny()*timeSimbox->getnz());
            LogKit::LogFormatted(LogKit::Low,"  FFT grid            %4i * %4i * %4i   :%11llu\n",
                                 modelSettings->getNXpad(),modelSettings->getNYpad(),modelSettings->getNZpad(),
                                 static_cast<unsigned long long int>(modelSettings->getNXpad())*modelSettings->getNYpad()*modelSettings->getNZpad());
          }

          //
          // Make time simbox with constant thicknesses (needed for log filtering and facies probabilities)
          //
          timeSimboxConstThick = new Simbox(timeSimbox);
          Surface tsurf(dynamic_cast<const Surface &> (timeSimbox->GetTopSurface()));
          timeSimboxConstThick->setDepth(tsurf, 0, timeSimbox->getlz(), timeSimbox->getdz());

          if((modelSettings->getOtherOutputFlag() & IO::EXTRA_SURFACES) > 0 && (modelSettings->getOutputGridDomain() & IO::TIMEDOMAIN) > 0) {
            std::string topSurf  = IO::PrefixSurface() + IO::PrefixTop()  + IO::PrefixTime() + "_ConstThick";
            std::string baseSurf = IO::PrefixSurface() + IO::PrefixBase() + IO::PrefixTime() + "_ConstThick";
            timeSimboxConstThick->writeTopBotGrids(topSurf,
                                                   baseSurf,
                                                   IO::PathToInversionResults(),
                                                   modelSettings->getOutputGridFormat());
          }
        }
        else
        {
          errText += "Could not make time simulation grid.\n";
          failed = true;
        }
      }
      else
      {
        timeSimbox->externalFailure();
        failed = true;
      }
    }
  }
}


void
ModelGeneral::logIntervalInformation(const Simbox      * simbox,
                                     const std::string & header_text1,
                                     const std::string & header_text2)
{
  LogKit::LogFormatted(LogKit::Low,"\n"+header_text1+"\n");
  double zmin, zmax;
  simbox->getMinMaxZ(zmin,zmax);
  LogKit::LogFormatted(LogKit::Low," %13s          avg / min / max    : %7.1f /%7.1f /%7.1f\n",
                       header_text2.c_str(),
                       zmin+simbox->getlz()*simbox->getAvgRelThick()*0.5,
                       zmin,zmax);
  LogKit::LogFormatted(LogKit::Low,"  Interval thickness    avg / min / max    : %7.1f /%7.1f /%7.1f\n",
                       simbox->getlz()*simbox->getAvgRelThick(),
                       simbox->getlz()*simbox->getMinRelThick(),
                       simbox->getlz());
  LogKit::LogFormatted(LogKit::Low,"  Sampling density      avg / min / max    : %7.2f /%7.2f /%7.2f\n",
                       simbox->getdz()*simbox->getAvgRelThick(),
                       simbox->getdz(),
                       simbox->getdz()*simbox->getMinRelThick());
}

void
ModelGeneral::setSimboxSurfaces(Simbox                        *& simbox,
                                const std::vector<std::string> & surfFile,
                                ModelSettings                  * modelSettings,
                                std::string                    & errText,
                                bool                           & failed)
{
  const std::string & topName = surfFile[0];

  bool   generateSeismic    = modelSettings->getForwardModeling();
  bool   estimationMode     = modelSettings->getEstimationMode();
  bool   generateBackground = modelSettings->getGenerateBackground();
  bool   parallelSurfaces   = modelSettings->getParallelTimeSurfaces();
  int    nz                 = modelSettings->getTimeNz();
  int    outputFormat       = modelSettings->getOutputGridFormat();
  int    outputDomain       = modelSettings->getOutputGridDomain();
  int    outputGridsElastic = modelSettings->getOutputGridsElastic();
  int    outputGridsOther   = modelSettings->getOutputGridsOther();
  int    outputGridsSeismic = modelSettings->getOutputGridsSeismic();
  double dTop               = modelSettings->getTimeDTop();
  double lz                 = modelSettings->getTimeLz();
  double dz                 = modelSettings->getTimeDz();

  Surface * z0Grid = NULL;
  Surface * z1Grid = NULL;
  try {
    if (NRLib::IsNumber(topName)) {
      // Find the smallest surface that covers the simbox. For simplicity
      // we use only four nodes (nx=ny=2).
      double xMin, xMax;
      double yMin, yMax;
      findSmallestSurfaceGeometry(simbox->getx0(), simbox->gety0(),
                                  simbox->getlx(), simbox->getly(),
                                  simbox->getAngle(),
                                  xMin,yMin,xMax,yMax);
      z0Grid = new Surface(xMin-100, yMin-100, xMax-xMin+200, yMax-yMin+200, 2, 2, atof(topName.c_str()));
    }
    else {
      Surface tmpSurf(topName);
      z0Grid = new Surface(tmpSurf);
    }
  }
  catch (NRLib::Exception & e) {
    errText += e.what();
    failed = true;
  }

  if(!failed) {
    if(parallelSurfaces) { //Only one reference surface
      simbox->setDepth(*z0Grid, dTop, lz, dz, modelSettings->getRunFromPanel());
    }
    else {
      const std::string & baseName = surfFile[1];
      try {
        if (NRLib::IsNumber(baseName)) {
          // Find the smallest surface that covers the simbox. For simplicity
          // we use only four nodes (nx=ny=2).
          double xMin, xMax;
          double yMin, yMax;
          findSmallestSurfaceGeometry(simbox->getx0(), simbox->gety0(),
                                      simbox->getlx(), simbox->getly(),
                                      simbox->getAngle(),
                                      xMin,yMin,xMax,yMax);
          z1Grid = new Surface(xMin-100, yMin-100, xMax-xMin+200, yMax-yMin+200, 2, 2, atof(baseName.c_str()));
        }
        else {
          Surface tmpSurf(baseName);
          z1Grid = new Surface(tmpSurf);
        }
      }
      catch (NRLib::Exception & e) {
        errText += e.what();
        failed = true;
      }
      if(!failed) {
        try {
          simbox->setDepth(*z0Grid, *z1Grid, nz, modelSettings->getRunFromPanel());
        }
        catch (NRLib::Exception & e) {
          errText += e.what();
          std::string text("Seismic data");
          writeAreas(modelSettings->getAreaParameters(),simbox,text);
          failed = true;
        }
      }
    }
    if (!failed) {
      if((outputDomain & IO::TIMEDOMAIN) > 0) {
        std::string topSurf  = IO::PrefixSurface() + IO::PrefixTop()  + IO::PrefixTime();
        std::string baseSurf = IO::PrefixSurface() + IO::PrefixBase() + IO::PrefixTime();
        simbox->setTopBotName(topSurf,baseSurf,outputFormat);
        if (generateSeismic) {
          simbox->writeTopBotGrids(topSurf,
                                   baseSurf,
                                   IO::PathToSeismicData(),
                                   outputFormat);
        }
        else if (!estimationMode){
          if (outputGridsElastic > 0 || outputGridsOther > 0 || outputGridsSeismic > 0)
            simbox->writeTopBotGrids(topSurf,
                                     baseSurf,
                                     IO::PathToInversionResults(),
                                     outputFormat);
        }
        if((outputFormat & IO::STORM) > 0) { // These copies are only needed with the STORM format
          if ((outputGridsElastic & IO::BACKGROUND) > 0 ||
              (outputGridsElastic & IO::BACKGROUND_TREND) > 0 ||
              (estimationMode && generateBackground)) {
            simbox->writeTopBotGrids(topSurf,
                                     baseSurf,
                                     IO::PathToBackground(),
                                     outputFormat);
          }
          if ((outputGridsOther & IO::CORRELATION) > 0) {
            simbox->writeTopBotGrids(topSurf,
                                     baseSurf,
                                     IO::PathToCorrelations(),
                                     outputFormat);
          }
          if ((outputGridsSeismic & (IO::ORIGINAL_SEISMIC_DATA | IO::SYNTHETIC_SEISMIC_DATA)) > 0) {
            simbox->writeTopBotGrids(topSurf,
                                     baseSurf,
                                     IO::PathToSeismicData(),
                                     outputFormat);
          }
          if ((outputGridsOther & IO::TIME_TO_DEPTH_VELOCITY) > 0) {
            simbox->writeTopBotGrids(topSurf,
                                     baseSurf,
                                     IO::PathToVelocity(),
                                     outputFormat);
          }
        }
      }
    }
  }
  delete z0Grid;
  delete z1Grid;
}

void
ModelGeneral::setupExtendedTimeSimbox(Simbox   * timeSimbox,
                                      Surface  * corrSurf,
                                      Simbox  *& timeCutSimbox,
                                      int        outputFormat,
                                      int        outputDomain,
                                      int        otherOutput)
{
  timeCutSimbox = new Simbox(timeSimbox);
  double * corrPlanePars = findPlane(corrSurf);
  Surface * meanSurf;
  if(corrSurf->GetNI() > 2)
    meanSurf = new Surface(*corrSurf);
  else {
    meanSurf = new Surface(dynamic_cast<const Surface &>(timeSimbox->GetTopSurface()));
    if(meanSurf->GetNI() == 2) { //Extend corrSurf to cover other surfaces.
      double minX = meanSurf->GetXMin();
      double maxX = meanSurf->GetXMax();
      double minY = meanSurf->GetYMin();
      double maxY = meanSurf->GetYMax();
      if(minX > corrSurf->GetXMin())
        minX = corrSurf->GetXMin();
      if(maxX < corrSurf->GetXMax())
        maxX = corrSurf->GetXMax();
      if(minY > corrSurf->GetYMin())
        minY = corrSurf->GetYMin();
      if(maxY < corrSurf->GetYMax())
        maxY = corrSurf->GetYMax();
      corrSurf->SetDimensions(minX, minY, maxX-minX, maxY-minY);
    }
  }
  int i;
  for(i=0;i<static_cast<int>(meanSurf->GetN());i++)
    (*meanSurf)(i) = 0;

  meanSurf->AddNonConform(&(timeSimbox->GetTopSurface()));
  meanSurf->AddNonConform(&(timeSimbox->GetBotSurface()));
  meanSurf->Multiply(0.5);
  double * refPlanePars = findPlane(meanSurf);

  for(i=0;i<3;i++)
    refPlanePars[i] -= corrPlanePars[i];
  gradX_ = refPlanePars[1];
  gradY_ = refPlanePars[2];

  Surface * refPlane = createPlaneSurface(refPlanePars, meanSurf);
  delete meanSurf;
  meanSurf = NULL;

  std::string fileName = "Correlation_Rotation_Plane";
  IO::writeSurfaceToFile(*refPlane, fileName, IO::PathToCorrelations(), outputFormat);

  refPlane->AddNonConform(corrSurf);
  delete [] corrPlanePars;
  delete [] refPlanePars;

  Surface topSurf(*refPlane);
  topSurf.SubtractNonConform(&(timeSimbox->GetTopSurface()));
  double shiftTop = topSurf.Max();
  shiftTop *= -1.0;
  topSurf.Add(shiftTop);
  topSurf.AddNonConform(&(timeSimbox->GetTopSurface()));

  Surface botSurf(*refPlane);
  botSurf.SubtractNonConform(&(timeSimbox->GetBotSurface()));
  double shiftBot = botSurf.Min();
  shiftBot *= -1.0;
  double thick    = shiftBot-shiftTop;
  double dz       = timeCutSimbox->getdz();
  int    nz       = int(thick/dz);
  double residual = thick - nz*dz;
  if (residual > 0.0) {
    shiftBot += dz-residual;
    nz++;
  }
  if (nz != timeCutSimbox->getnz()) {
    LogKit::LogFormatted(LogKit::High,"\nNumber of layers in inversion increased from %d",timeCutSimbox->getnz());
    LogKit::LogFormatted(LogKit::High," to %d in grid created using correlation direction.\n",nz);
  }
  botSurf.Add(shiftBot);
  botSurf.AddNonConform(&(timeSimbox->GetBotSurface()));

  timeSimbox->setDepth(topSurf, botSurf, nz);

  if((otherOutput & IO::EXTRA_SURFACES) > 0 && (outputDomain & IO::TIMEDOMAIN) > 0) {
    std::string topSurf  = IO::PrefixSurface() + IO::PrefixTop()  + IO::PrefixTime() + "_Extended";
    std::string baseSurf = IO::PrefixSurface() + IO::PrefixBase() + IO::PrefixTime() + "_Extended";
    timeSimbox->writeTopBotGrids(topSurf,
                                 baseSurf,
                                 IO::PathToInversionResults(),
                                 outputFormat);
  }

  delete refPlane;
}

void
ModelGeneral::setupExtendedBackgroundSimbox(Simbox   * timeSimbox,
                                            Surface  * corrSurf,
                                            Simbox  *& timeBGSimbox,
                                            int        outputFormat,
                                            int        outputDomain,
                                            int        otherOutput)
{
  //
  // Move correlation surface for easier handling.
  //
  Surface tmpSurf(*corrSurf);
  double avg = tmpSurf.Avg();
  if (avg > 0)
    tmpSurf.Subtract(avg);
  else
    tmpSurf.Add(avg); // This situation is not very likely, but ...

  //
  // Find top surface of background simbox.
  //
  // The funny/strange dTop->Multiply(-1.0) is due to NRLIB's current
  // inability to set dTop equal to Simbox top surface.
  //
  Surface dTop(tmpSurf);
  dTop.SubtractNonConform(&(timeSimbox->GetTopSurface()));
  dTop.Multiply(-1.0);
  double shiftTop = dTop.Min();
  Surface topSurf(tmpSurf);
  topSurf.Add(shiftTop);

  //
  // Find base surface of background simbox
  //
  Surface dBot(tmpSurf);
  dBot.SubtractNonConform(&(timeSimbox->GetBotSurface()));
  dBot.Multiply(-1.0);
  double shiftBot = dBot.Max();
  Surface botSurf(tmpSurf);
  botSurf.Add(shiftBot);

  //
  // Calculate number of layers of background simbox
  //
  tmpSurf.Assign(0.0);
  tmpSurf.AddNonConform(&botSurf);
  tmpSurf.SubtractNonConform(&topSurf);
  double dMax = tmpSurf.Max();
  double dt = timeSimbox->getdz();
  int nz;
  //
  // NBNB-PAL: I think it is a good idea to use a maximum dt of 10ms.
  //
  //if (dt < 10.0) {
  //  LogKit::LogFormatted(LogKit::High,"\nReducing sampling density for background",dt);
  //  LogKit::LogFormatted(LogKit::High," modelling from %.2fms to 10.0ms\n");
  //  dt = 10.0;  // A sampling density of 10.0ms is good enough for BG model
  // }
  nz = static_cast<int>(ceil(dMax/dt));

  //
  // Make new simbox
  //
  timeBGSimbox = new Simbox(timeSimbox);
  timeBGSimbox->setDepth(topSurf, botSurf, nz);

  if((otherOutput & IO::EXTRA_SURFACES) > 0 && (outputDomain & IO::TIMEDOMAIN) > 0) {
    std::string topSurf  = IO::PrefixSurface() + IO::PrefixTop()  + IO::PrefixTime() + "_BG";
    std::string baseSurf = IO::PrefixSurface() + IO::PrefixBase() + IO::PrefixTime() + "_BG";
    timeBGSimbox->writeTopBotGrids(topSurf,
                                   baseSurf,
                                   IO::PathToBackground(),
                                   outputFormat);
  }
}

double *
ModelGeneral::findPlane(Surface * surf)
{
  double ** A = new double * [3];
  double * b = new double[3];
  int i, j;
  for(i=0;i<3;i++) {
    A[i] = new double[3];
    for(j=0;j<3;j++)
      A[i][j] = 0;
    b[i] = 0;
  }

  double x, y, z;
  int nData = 0;
  for(i=0;i<static_cast<int>(surf->GetN());i++) {
    surf->GetXY(i, x, y);
    z = (*surf)(i);
    if(!surf->IsMissing(z)) {
      nData++;
      A[0][1] += x;
      A[0][2] += y;
      A[1][1] += x*x;
      A[1][2] += x*y;
      A[2][2] += y*y;
      b[0] += z;
      b[1] += x*z;
      b[2] += y*z;
    }
  }

  A[0][0] = nData;
  A[1][0] = A[0][1];
  A[2][0] = A[0][2];
  A[2][1] = A[1][2];

  lib_matrCholR(3, A);
  lib_matrAxeqbR(3, A, b);

  for(i=0;i<3;i++)
    delete [] A[i];
  delete [] A;

  return(b);
}


Surface *
ModelGeneral::createPlaneSurface(double * planeParams, Surface * templateSurf)
{
  Surface * result = new Surface(*templateSurf);
  double x,y;
  int i;
  for(i=0;i<static_cast<int>(result->GetN());i++) {
    result->GetXY(i,x,y);
    (*result)(i) = planeParams[0]+planeParams[1]*x+planeParams[2]*y;
  }
  return(result);
}


void
ModelGeneral::estimateXYPaddingSizes(Simbox         * timeSimbox,
                                     ModelSettings *& modelSettings)
{
  double dx      = timeSimbox->getdx();
  double dy      = timeSimbox->getdy();
  double lx      = timeSimbox->getlx();
  double ly      = timeSimbox->getly();
  int    nx      = timeSimbox->getnx();
  int    ny      = timeSimbox->getny();
  int    nz      = timeSimbox->getnz();

  double xPadFac = modelSettings->getXPadFac();
  double yPadFac = modelSettings->getYPadFac();
  double xPad    = xPadFac*lx;
  double yPad    = yPadFac*ly;

  if (modelSettings->getEstimateXYPadding())
  {
    float  range1 = modelSettings->getLateralCorr()->getRange();
    float  range2 = modelSettings->getLateralCorr()->getSubRange();
    float  angle  = modelSettings->getLateralCorr()->getAngle();
    double factor = 0.5;  // Lateral correlation is not very important. Half a range is probably more than enough

    xPad          = factor * std::max(fabs(range1*cos(angle)), fabs(range2*sin(angle)));
    yPad          = factor * std::max(fabs(range1*sin(angle)), fabs(range2*cos(angle)));
    xPad          = std::max(xPad, dx);     // Always require at least on grid cell
    yPad          = std::max(yPad, dy);     // Always require at least one grid cell
    xPadFac       = std::min(1.0, xPad/lx); // A padding of more than 100% is insensible
    yPadFac       = std::min(1.0, yPad/ly);
  }

  int nxPad = setPaddingSize(nx, xPadFac);
  int nyPad = setPaddingSize(ny, yPadFac);
  int nzPad = modelSettings->getNZpad();

  double true_xPadFac = static_cast<double>(nxPad - nx)/static_cast<double>(nx);
  double true_yPadFac = static_cast<double>(nyPad - ny)/static_cast<double>(ny);
  double true_zPadFac = modelSettings->getZPadFac();
  double true_xPad    = true_xPadFac*lx;
  double true_yPad    = true_yPadFac*ly;
  double true_zPad    = true_zPadFac*(timeSimbox->getlz()*timeSimbox->getMinRelThick());

  modelSettings->setNXpad(nxPad);
  modelSettings->setNYpad(nyPad);
  modelSettings->setXPadFac(true_xPadFac);
  modelSettings->setYPadFac(true_yPadFac);

  std::string text1;
  std::string text2;
  int logLevel = LogKit::Medium;
  if (modelSettings->getEstimateXYPadding()) {
    text1 = " estimated from lateral correlation ranges in internal grid";
    logLevel = LogKit::Low;
  }
  if (modelSettings->getEstimateZPadding()) {
    text2 = " estimated from an assumed wavelet length";
    logLevel = LogKit::Low;
  }

  LogKit::LogFormatted(logLevel,"\nPadding sizes"+text1+":\n");
  LogKit::LogFormatted(logLevel,"  xPad, xPadFac, nx, nxPad                 : %6.fm, %5.3f, %5d, %4d\n",
                       true_xPad, true_xPadFac, nx, nxPad);
  LogKit::LogFormatted(logLevel,"  yPad, yPadFac, ny, nyPad                 : %6.fm, %5.3f, %5d, %4d\n",
                       true_yPad, true_yPadFac, ny, nyPad);
  LogKit::LogFormatted(logLevel,"\nPadding sizes"+text2+":\n");
  LogKit::LogFormatted(logLevel,"  zPad, zPadFac, nz, nzPad                 : %5.fms, %5.3f, %5d, %4d\n",
                       true_zPad, true_zPadFac, nz, nzPad);
}

void
ModelGeneral::estimateZPaddingSize(Simbox         * timeSimbox,
                                   ModelSettings *& modelSettings)
{
  int    nz          = timeSimbox->getnz();
  double minLz       = timeSimbox->getlz()*timeSimbox->getMinRelThick();
  double zPadFac     = modelSettings->getZPadFac();
  double zPad        = zPadFac*minLz;

  if (modelSettings->getEstimateZPadding())
  {
    double wLength = static_cast<double>(modelSettings->getDefaultWaveletLength());
    double pfac    = 1.0;
    zPad           = wLength/pfac;                             // Use half a wavelet as padding
    zPadFac        = std::min(1.0, zPad/minLz);                // More than 100% padding is not sensible
  }
  int nzPad        = setPaddingSize(nz, zPadFac);
  zPadFac          = static_cast<double>(nzPad - nz)/static_cast<double>(nz);

  modelSettings->setNZpad(nzPad);
  modelSettings->setZPadFac(zPadFac);
}

void
ModelGeneral::readGridFromFile(const std::string       & fileName,
                               const std::string       & parName,
                               const float               offset,
                               FFTGrid                *& grid,
                               const SegyGeometry     *& geometry,
                               const TraceHeaderFormat * format,
                               int                       gridType,
                               Simbox                  * timeSimbox,
                               Simbox                  * timeCutSimbox,
                               ModelSettings           * modelSettings,
                               std::string             & errText,
                               bool                      nopadding)
{
  int fileType = IO::findGridType(fileName);

  if(fileType == IO::CRAVA)
  {
    int nxPad, nyPad, nzPad;
    if(nopadding)
    {
      nxPad = timeSimbox->getnx();
      nyPad = timeSimbox->getny();
      nzPad = timeSimbox->getnz();
    }
    else
    {
      nxPad = modelSettings->getNXpad();
      nyPad = modelSettings->getNYpad();
      nzPad = modelSettings->getNZpad();
    }
    LogKit::LogFormatted(LogKit::Low,"\nReading grid \'"+parName+"\' from file "+fileName);
    grid = createFFTGrid(timeSimbox->getnx(),
                         timeSimbox->getny(),
                         timeSimbox->getnz(),
                         nxPad,
                         nyPad,
                         nzPad,
                         modelSettings->getFileGrid());

    grid->setType(gridType);
    grid->readCravaFile(fileName, errText, nopadding);
  }
  else if(fileType == IO::SEGY)
    readSegyFile(fileName, grid, timeSimbox, timeCutSimbox, modelSettings, geometry,
                 gridType, offset, format, errText, nopadding);
  else if(fileType == IO::STORM)
    readStormFile(fileName, grid, gridType, parName, timeSimbox, modelSettings, errText, false, nopadding);
  else if(fileType == IO::SGRI)
    readStormFile(fileName, grid, gridType, parName, timeSimbox, modelSettings, errText, true, nopadding);
  else
  {
    errText += "\nReading of file \'"+fileName+"\' for grid type \'"+parName+"\'failed. File type not recognized.\n";
  }

}

void
ModelGeneral::printSettings(ModelSettings     * modelSettings,
                            const InputFiles  * inputFiles)
{
  LogKit::WriteHeader("Model settings");

  LogKit::LogFormatted(LogKit::Low,"\nGeneral settings:\n");
  if(modelSettings->getForwardModeling()==true)
    LogKit::LogFormatted(LogKit::Low,"  Modelling mode                           : forward\n");
  else if (modelSettings->getEstimationMode()==true)
    LogKit::LogFormatted(LogKit::Low,"  Modelling mode                           : estimation\n");

  else if (modelSettings->getNumberOfSimulations() == 0)
    LogKit::LogFormatted(LogKit::Low,"  Modelling mode                           : prediction\n");
  else
  {
    LogKit::LogFormatted(LogKit::Low,"  Modelling mode                           : simulation\n");
    if(inputFiles->getSeedFile()=="") {
      if (modelSettings->getSeed() == 0)
        LogKit::LogFormatted(LogKit::Low,"  Seed                                     :          0 (default seed)\n");
      else
        LogKit::LogFormatted(LogKit::Low,"  Seed                                     : %10d\n",modelSettings->getSeed());
    }
    else
      LogKit::LogFormatted(LogKit::Low,"  Seed read from file                      : %10s\n",inputFiles->getSeedFile().c_str());


    LogKit::LogFormatted(LogKit::Low,"  Number of realisations                   : %10d\n",modelSettings->getNumberOfSimulations());
  }
  if(modelSettings->getForwardModeling()==false)
  {
    LogKit::LogFormatted(LogKit::Low,"  Kriging                                  : %10s\n",(modelSettings->getKrigingParameter()>0 ? "yes" : "no"));
    LogKit::LogFormatted(LogKit::Low,"  Facies probabilities                     : %10s\n",(modelSettings->getEstimateFaciesProb() ? "yes" : "no"));
    LogKit::LogFormatted(LogKit::Low,"  Synthetic seismic                        : %10s\n",(modelSettings->getGenerateSeismicAfterInv() ? "yes" : "no" ));
  }

  if (modelSettings->getEstimateFaciesProb()) {
    LogKit::LogFormatted(LogKit::Low,"\nSettings for facies probability estimation:\n");
    LogKit::LogFormatted(LogKit::Low,"  Use elastic parameters relative to trend : %10s\n",(modelSettings->getFaciesProbRelative()     ? "yes" : "no"));
    LogKit::LogFormatted(LogKit::Low,"  Include Vs information in estimation     : %10s\n",(modelSettings->getNoVsFaciesProb()         ? "no"  : "yes"));
    LogKit::LogFormatted(LogKit::Low,"  Use filtered well logs for estimation    : %10s\n",(modelSettings->getUseFilterForFaciesProb() ? "yes" : "no"));
  }

  LogKit::LogFormatted(LogKit::Low,"\nInput/Output settings:\n");
  std::string logText("*NONE*");
  int logLevel = modelSettings->getLogLevel();
  if (logLevel == LogKit::L_Error)
    logText = "ERROR";
  else if (logLevel == LogKit::L_Warning)
    logText = "WARNING";
  else if (logLevel == LogKit::L_Low)
    logText = "LOW";
  else if (logLevel == LogKit::L_Medium)
    logText = "MEDIUM";
  else if (logLevel == LogKit::L_High)
    logText = "HIGH";
  else if (logLevel == LogKit::L_DebugLow)
     logText = "DEBUGLOW";
  else if (logLevel == LogKit::L_DebugHigh)
    logText = "DEBUGHIGH";
  LogKit::LogFormatted(LogKit::Low, "  Log level                                : %10s\n",logText.c_str());
  if (inputFiles->getInputDirectory() != "")
    LogKit::LogFormatted(LogKit::High,"  Input directory                          : %10s\n",inputFiles->getInputDirectory().c_str());
  if (IO::getOutputPath() != "")
    LogKit::LogFormatted(LogKit::High,"  Output directory                         : %10s\n",IO::getOutputPath().c_str());

  int gridFormat         = modelSettings->getOutputGridFormat();
  int gridDomain         = modelSettings->getOutputGridDomain();
  int outputGridsOther   = modelSettings->getOutputGridsOther();
  int outputGridsElastic = modelSettings->getOutputGridsElastic();
  int outputGridsSeismic = modelSettings->getOutputGridsSeismic();

  if (outputGridsElastic > 0  || outputGridsSeismic > 0  || outputGridsOther > 0) {
    LogKit::LogFormatted(LogKit::Medium,"\nGrid output formats:\n");
    if (gridFormat & IO::SEGY) {
      const std::string & formatName = modelSettings->getTraceHeaderFormatOutput()->GetFormatName();
      LogKit::LogFormatted(LogKit::Medium,"  Segy - %-10s                        :        yes\n",formatName.c_str());
    }
    if (gridFormat & IO::STORM)
      LogKit::LogFormatted(LogKit::Medium,"  Storm                                    :        yes\n");
    if (gridFormat & IO::ASCII)
      LogKit::LogFormatted(LogKit::Medium,"  ASCII                                    :        yes\n");
    if (gridFormat & IO::SGRI)
      LogKit::LogFormatted(LogKit::Medium,"  Norsar                                   :        yes\n");
    if (gridFormat & IO::CRAVA)
      LogKit::LogFormatted(LogKit::Medium,"  Crava                                    :        yes\n");

    LogKit::LogFormatted(LogKit::Medium,"\nGrid output domains:\n");
    if (gridDomain & IO::TIMEDOMAIN)
      LogKit::LogFormatted(LogKit::Medium,"  Time                                     :        yes\n");
    if (gridDomain & IO::DEPTHDOMAIN)
      LogKit::LogFormatted(LogKit::Medium,"  Depth                                    :        yes\n");
  }

  if (outputGridsElastic > 0 &&
      modelSettings->getForwardModeling() == false) {
    LogKit::LogFormatted(LogKit::Medium,"\nOutput of elastic parameters:\n");
    if ((outputGridsElastic & IO::VP) > 0)
      LogKit::LogFormatted(LogKit::Medium,"  Pressure-wave velocity  (Vp)             :        yes\n");
    if ((outputGridsElastic & IO::VS) > 0)
      LogKit::LogFormatted(LogKit::Medium,"  Shear-wave velocity  (Vs)                :        yes\n");
    if ((outputGridsElastic & IO::RHO) > 0)
      LogKit::LogFormatted(LogKit::Medium,"  Density  (Rho)                           :        yes\n");
    if ((outputGridsElastic & IO::AI) > 0)
      LogKit::LogFormatted(LogKit::Medium,"  Acoustic impedance  (AI)                 :        yes\n");
    if ((outputGridsElastic & IO::VPVSRATIO) > 0)
      LogKit::LogFormatted(LogKit::Medium,"  Vp/Vs ratio                              :        yes\n");
    if ((outputGridsElastic & IO::SI) > 0)
      LogKit::LogFormatted(LogKit::Medium,"  Shear impedance  (SI)                    :        yes\n");
    if ((outputGridsElastic & IO::MURHO) > 0)
      LogKit::LogFormatted(LogKit::Medium,"  MuRho  (SI*SI)                           :        yes\n");
    if ((outputGridsElastic & IO::LAMBDARHO) > 0)
      LogKit::LogFormatted(LogKit::Medium,"  LambdaRho  (AI*AI - 2*SI*SI)             :        yes\n");
    if ((outputGridsElastic & IO::LAMELAMBDA) > 0)
      LogKit::LogFormatted(LogKit::Medium,"  Lame's first parameter                   :        yes\n");
    if ((outputGridsElastic & IO::LAMEMU) > 0)
      LogKit::LogFormatted(LogKit::Medium,"  Lame's second parameter (shear modulus)  :        yes\n");
    if ((outputGridsElastic & IO::POISSONRATIO) > 0)
      LogKit::LogFormatted(LogKit::Medium,"  Poisson ratio  (X-1)/2(X-2), X=(Vp/Vs)^2 :        yes\n");
    if ((outputGridsElastic & IO::BACKGROUND) > 0)
      LogKit::LogFormatted(LogKit::Medium,"  Background (Vp, Vs, Rho)                 :        yes\n");
    if ((outputGridsElastic & IO::BACKGROUND_TREND) > 0)
      LogKit::LogFormatted(LogKit::Medium,"  Background trend (Vp, Vs, Rho)           :        yes\n");
  }

  if (modelSettings->getForwardModeling() ||
      outputGridsSeismic > 0) {
    LogKit::LogFormatted(LogKit::Medium,"\nOutput of seismic data:\n");
    if ((outputGridsSeismic & IO::SYNTHETIC_SEISMIC_DATA) > 0 || modelSettings->getForwardModeling())
      LogKit::LogFormatted(LogKit::Medium,"  Synthetic seismic data (forward modelled):        yes\n");
    if ((outputGridsSeismic & IO::ORIGINAL_SEISMIC_DATA) > 0)
      LogKit::LogFormatted(LogKit::Medium,"  Original seismic data (in output grid)   :        yes\n");
    if ((outputGridsSeismic & IO::RESIDUAL) > 0)
      LogKit::LogFormatted(LogKit::Medium,"  Seismic data residuals                   :        yes\n");
  }

  if (modelSettings->getEstimateFaciesProb()) {
    LogKit::LogFormatted(LogKit::Medium,"\nOutput of facies probability volumes:\n");
    if ((outputGridsOther & IO::FACIESPROB) > 0)
      LogKit::LogFormatted(LogKit::Medium,"  Facies probabilities                     :        yes\n");
    if ((outputGridsOther & IO::FACIESPROB_WITH_UNDEF) > 0)
      LogKit::LogFormatted(LogKit::Medium,"  Facies probabilities with undefined value:        yes\n");
  }

  if ((outputGridsOther & IO::CORRELATION)>0 ||
      (outputGridsOther & IO::EXTRA_GRIDS)  >0 ||
      (outputGridsOther & IO::TIME_TO_DEPTH_VELOCITY)>0) {
    LogKit::LogFormatted(LogKit::Medium,"\nOther grid output:\n");
    if ((outputGridsOther & IO::CORRELATION) > 0)
      LogKit::LogFormatted(LogKit::Medium,"  Posterior correlations                   :        yes\n");
    if ((outputGridsOther & IO::EXTRA_GRIDS) > 0)
      LogKit::LogFormatted(LogKit::Medium,"  Help grids (see use manual)              :        yes\n");
    if ((outputGridsOther & IO::TIME_TO_DEPTH_VELOCITY) > 0)
      LogKit::LogFormatted(LogKit::Medium,"  Time-to-depth velocity                   :        yes\n");
  }

  if (modelSettings->getFileGrid())
    LogKit::LogFormatted(LogKit::Medium,"\nAdvanced settings:\n");
  else
    LogKit::LogFormatted(LogKit::High,"\nAdvanced settings:\n");

  LogKit::LogFormatted(LogKit::Medium, "  Use intermediate disk storage for grids  : %10s\n", (modelSettings->getFileGrid() ? "yes" : "no"));

  if (inputFiles->getReflMatrFile() != "")
    LogKit::LogFormatted(LogKit::Medium, "  Take reflection matrix from file         : %10s\n", inputFiles->getReflMatrFile().c_str());

  if (modelSettings->getVpVsRatio() != RMISSING)
    LogKit::LogFormatted(LogKit::High ,"  Vp-Vs ratio used in reflection coef.     : %10.2f\n", modelSettings->getVpVsRatio());

  LogKit::LogFormatted(LogKit::High, "  RMS panel mode                           : %10s\n"  , (modelSettings->getRunFromPanel() ? "yes" : "no"));
  LogKit::LogFormatted(LogKit::High ,"  Smallest allowed length increment (dxy)  : %10.2f\n", modelSettings->getMinHorizontalRes());
  LogKit::LogFormatted(LogKit::High ,"  Smallest allowed time increment (dt)     : %10.2f\n", modelSettings->getMinSamplingDensity());

  if (modelSettings->getKrigingParameter()>0) { // We are doing kriging
    LogKit::LogFormatted(LogKit::High ,"  Data in neighbourhood when doing kriging : %10.2f\n", modelSettings->getKrigingParameter());
    LogKit::LogFormatted(LogKit::High, "  Smooth kriged parameters                 : %10s\n", (modelSettings->getDoSmoothKriging() ? "yes" : "no"));
  }

  LogKit::LogFormatted(LogKit::High,"\nUnit settings/assumptions:\n");
  LogKit::LogFormatted(LogKit::High,"  Time                                     : %10s\n","ms TWT");
  LogKit::LogFormatted(LogKit::High,"  Frequency                                : %10s\n","Hz");
  LogKit::LogFormatted(LogKit::High,"  Length                                   : %10s\n","m");
  LogKit::LogFormatted(LogKit::High,"  Velocities                               : %10s\n","m/s");
  LogKit::LogFormatted(LogKit::High,"  Density                                  : %10s\n","g/cm3");
  LogKit::LogFormatted(LogKit::High,"  Angles                                   : %10s\n","   degrees (clockwise relative to north when applicable)");

  //
  // WELL PROCESSING
  //
  if (modelSettings->getNumberOfWells() > 0)
  {
    LogKit::LogFormatted(LogKit::High,"\nSettings for well processing:\n");
    LogKit::LogFormatted(LogKit::High,"  Threshold for merging log entries        : %10.2f ms\n",modelSettings->getMaxMergeDist());
    LogKit::LogFormatted(LogKit::High,"  Threshold for Vp-Vs rank correlation     : %10.2f\n",modelSettings->getMaxRankCorr());
    LogKit::LogFormatted(LogKit::High,"  Threshold for deviation angle            : %10.1f (=%.2fm/ms TWT)\n",
                         modelSettings->getMaxDevAngle(),tan(modelSettings->getMaxDevAngle()*M_PI/180.0));
    LogKit::LogFormatted(LogKit::High,"  High cut for background modelling        : %10.1f\n",modelSettings->getMaxHzBackground());
    LogKit::LogFormatted(LogKit::High,"  High cut for seismic resolution          : %10.1f\n",modelSettings->getMaxHzSeismic());
    LogKit::LogFormatted(LogKit::High,"  Estimate Vp-Vs ratio from well data      : %10s\n", (modelSettings->getVpVsRatioFromWells() ? "yes" : "no"));
  }
  LogKit::LogFormatted(LogKit::High,"\nRange of allowed parameter values:\n");
  LogKit::LogFormatted(LogKit::High,"  Vp  - min                                : %10.0f\n",modelSettings->getAlphaMin());
  LogKit::LogFormatted(LogKit::High,"  Vp  - max                                : %10.0f\n",modelSettings->getAlphaMax());
  LogKit::LogFormatted(LogKit::High,"  Vs  - min                                : %10.0f\n",modelSettings->getBetaMin());
  LogKit::LogFormatted(LogKit::High,"  Vs  - max                                : %10.0f\n",modelSettings->getBetaMax());
  LogKit::LogFormatted(LogKit::High,"  Rho - min                                : %10.1f\n",modelSettings->getRhoMin());
  LogKit::LogFormatted(LogKit::High,"  Rho - max                                : %10.1f\n",modelSettings->getRhoMax());

  //
  // WELL DATA
  //
  if (modelSettings->getNumberOfWells() > 0)
  {
    LogKit::LogFormatted(LogKit::Low,"\nWell logs:\n");
    const std::vector<std::string> & logNames = modelSettings->getLogNames();

    if (logNames.size() > 0)
    {
      LogKit::LogFormatted(LogKit::Low,"  Time                                     : %10s\n",  logNames[0].c_str());
      if(NRLib::Uppercase(logNames[1])=="VP" ||
         NRLib::Uppercase(logNames[1])=="LFP_VP")
        LogKit::LogFormatted(LogKit::Low,"  p-wave velocity                          : %10s\n",logNames[1].c_str());
      else
        LogKit::LogFormatted(LogKit::Low,"  Sonic                                    : %10s\n",logNames[1].c_str());
      if(NRLib::Uppercase(logNames[3])=="VS" ||
         NRLib::Uppercase(logNames[3])=="LFP_VS")
        LogKit::LogFormatted(LogKit::Low,"  s-wave velocity                          : %10s\n",logNames[3].c_str());
      else
        LogKit::LogFormatted(LogKit::Low,"  Shear sonic                              : %10s\n",logNames[3].c_str());
      LogKit::LogFormatted(LogKit::Low,"  Density                                  : %10s\n",  logNames[2].c_str());
      if (modelSettings->getFaciesLogGiven())
        LogKit::LogFormatted(LogKit::Low,"  Facies                                   : %10s\n",logNames[4].c_str());
    }
    else
    {
      LogKit::LogFormatted(LogKit::Low,"  Time                                     : %10s\n","TWT");
      LogKit::LogFormatted(LogKit::Low,"  Sonic                                    : %10s\n","DT");
      LogKit::LogFormatted(LogKit::Low,"  Shear sonic                              : %10s\n","DTS");
      LogKit::LogFormatted(LogKit::Low,"  Density                                  : %10s\n","RHOB");
      LogKit::LogFormatted(LogKit::Low,"  Facies                                   : %10s\n","FACIES");
    }
    LogKit::LogFormatted(LogKit::Low,"\nWell files:\n");
    for (int i = 0 ; i < modelSettings->getNumberOfWells() ; i++)
    {
      LogKit::LogFormatted(LogKit::Low,"  %-2d                                       : %s\n",i+1,inputFiles->getWellFile(i).c_str());
    }
    bool generateBackground = modelSettings->getGenerateBackground();
    bool estimateFaciesProb = modelSettings->getFaciesLogGiven();
    bool estimateWavelet    = false;
    for(int i=0; i<modelSettings->getNumberOfTimeLapses(); i++){
      std::vector<bool> estimateWaveletAllTraces = modelSettings->getEstimateWavelet(i);
      for (int j = 0 ; j < modelSettings->getNumberOfAngles(i) ; j++)
        estimateWavelet = estimateWavelet || estimateWaveletAllTraces[j];
    }
    if (generateBackground || estimateFaciesProb || estimateWavelet)
    {
      LogKit::LogFormatted(LogKit::Low,"\nUse well in estimation of:                   ");
      if (generateBackground) LogKit::LogFormatted(LogKit::Low,"BackgroundTrend  ");
      if (estimateWavelet)    LogKit::LogFormatted(LogKit::Low,"WaveletEstimation  ");
      if (estimateFaciesProb) LogKit::LogFormatted(LogKit::Low,"FaciesProbabilities");
      LogKit::LogFormatted(LogKit::Low,"\n");
      for (int i = 0 ; i < modelSettings->getNumberOfWells() ; i++)
      {
        LogKit::LogFormatted(LogKit::Low,"  %-2d                                       : ",i+1);
        if (generateBackground) {
          if (modelSettings->getIndicatorBGTrend(i) == ModelSettings::YES)
            LogKit::LogFormatted(LogKit::Low,"    %-11s  ","yes");
          else if (modelSettings->getIndicatorBGTrend(i) == ModelSettings::NO)
            LogKit::LogFormatted(LogKit::Low,"    %-11s  ","no");
          else
            LogKit::LogFormatted(LogKit::Low,"    %-11s  ","yes");
        }
        if (estimateWavelet) {
          if (modelSettings->getIndicatorWavelet(i) == ModelSettings::YES)
            LogKit::LogFormatted(LogKit::Low,"    %-13s  ","yes");
          else if (modelSettings->getIndicatorWavelet(i) == ModelSettings::NO)
            LogKit::LogFormatted(LogKit::Low,"    %-13s  ","no");
          else
            LogKit::LogFormatted(LogKit::Low,"    %-12s  ","if possible");
        }
        if (estimateFaciesProb) {
          if (modelSettings->getIndicatorFacies(i) == ModelSettings::YES)
            LogKit::LogFormatted(LogKit::Low,"    %-12s","yes");
          else if (modelSettings->getIndicatorFacies(i) == ModelSettings::NO)
            LogKit::LogFormatted(LogKit::Low,"    %-12s","no");
          else
            LogKit::LogFormatted(LogKit::Low,"    %-12s","if possible");
        }
        LogKit::LogFormatted(LogKit::Low,"\n");
      }
    }
    if ( modelSettings->getOptimizeWellLocation() )
    {
      LogKit::LogFormatted(LogKit::Low,"\nFor well, optimize position for            : Angle with Weight\n");
      for (int i = 0 ; i < modelSettings->getNumberOfWells() ; i++)
      {
        int nMoveAngles = modelSettings->getNumberOfWellAngles(i);
        if( nMoveAngles > 0 )
        {
          LogKit::LogFormatted(LogKit::Low," %2d %46.1f %10.1f\n",i+1,(modelSettings->getWellMoveAngle(i,0)*180/M_PI),modelSettings->getWellMoveWeight(i,0));
          for (int j=1; j<nMoveAngles; j++)
            LogKit::LogFormatted(LogKit::Low," %49.1f %10.1f\n",(modelSettings->getWellMoveAngle(i,j)*180/M_PI),modelSettings->getWellMoveWeight(i,j));
        }
        LogKit::LogFormatted(LogKit::Low,"\n");
      }
    }
  }

  //
  // AREA
  //
  std::string gridFile;
  int areaSpecification = modelSettings->getAreaSpecification();
  if(modelSettings->getForwardModeling()) {
    LogKit::LogFormatted(LogKit::Low,"\nSeismic area:\n");
    gridFile = inputFiles->getBackFile(0);    // Get geometry from earth model (Vp)
  }
  else {
    LogKit::LogFormatted(LogKit::Low,"\nInversion area");
    if(areaSpecification == ModelSettings::AREA_FROM_GRID_DATA ||
       areaSpecification == ModelSettings::AREA_FROM_GRID_DATA_AND_UTM ||
       areaSpecification == ModelSettings::AREA_FROM_GRID_DATA_AND_SURFACE)
      gridFile = inputFiles->getSeismicFile(0,0); // Get area from first seismic data volume
  }
  if (areaSpecification == ModelSettings::AREA_FROM_GRID_DATA) {
    const std::vector<int> & areaILXL = modelSettings->getAreaILXL();
    LogKit::LogFormatted(LogKit::Low," taken from grid\n");
    LogKit::LogFormatted(LogKit::Low,"  Grid                                     : "+gridFile+"\n");
    if(areaILXL.size() > 0) {
      if (areaILXL[0] != IMISSING)
        LogKit::LogFormatted(LogKit::Low,"  In-line start                            : %10d\n", areaILXL[0]);
      if (areaILXL[1] != IMISSING)
        LogKit::LogFormatted(LogKit::Low,"  In-line end                              : %10d\n", areaILXL[1]);
      if (areaILXL[4] != IMISSING)
        LogKit::LogFormatted(LogKit::Low,"  In-line step                             : %10d\n", areaILXL[4]);
      if (areaILXL[2] != IMISSING)
        LogKit::LogFormatted(LogKit::Low,"  Cross-line start                         : %10d\n", areaILXL[2]);
      if (areaILXL[3] != IMISSING)
        LogKit::LogFormatted(LogKit::Low,"  Cross-line end                           : %10d\n", areaILXL[3]);
      if (areaILXL[5] != IMISSING)
        LogKit::LogFormatted(LogKit::Low,"  Cross-line step                          : %10d\n", areaILXL[5]);
    }
  }
  else if (areaSpecification == ModelSettings::AREA_FROM_UTM ||
           areaSpecification == ModelSettings::AREA_FROM_GRID_DATA_AND_UTM) {
    LogKit::LogFormatted(LogKit::Low," given as UTM coordinates\n");
    const SegyGeometry * geometry = modelSettings->getAreaParameters();
    LogKit::LogFormatted(LogKit::Low,"  Reference point x                        : %10.1f\n", geometry->GetX0());
    LogKit::LogFormatted(LogKit::Low,"  Reference point y                        : %10.1f\n", geometry->GetY0());
    LogKit::LogFormatted(LogKit::Low,"  Length x                                 : %10.1f\n", geometry->Getlx());
    LogKit::LogFormatted(LogKit::Low,"  Length y                                 : %10.1f\n", geometry->Getly());
    if (areaSpecification == ModelSettings::AREA_FROM_UTM) {
      LogKit::LogFormatted(LogKit::Low,"  Sample density x                         : %10.1f\n", geometry->GetDx());
      LogKit::LogFormatted(LogKit::Low,"  Sample density y                         : %10.1f\n", geometry->GetDy());
    }
    LogKit::LogFormatted(LogKit::Low,"  Rotation                                 : %10.4f\n", geometry->GetAngle()*(180.0/NRLib::Pi)*(-1));
    if (areaSpecification == ModelSettings::AREA_FROM_GRID_DATA_AND_UTM) {
      LogKit::LogFormatted(LogKit::Low,"and snapped to seismic data\n");
      LogKit::LogFormatted(LogKit::Low,"  Grid                                     : "+gridFile+"\n");
    }
  }
  else if (areaSpecification == ModelSettings::AREA_FROM_SURFACE) {
    LogKit::LogFormatted(LogKit::Low," taken from surface\n");
    LogKit::LogFormatted(LogKit::Low,"  Reference surface                        : "+inputFiles->getAreaSurfaceFile()+"\n");
    if (areaSpecification == ModelSettings::AREA_FROM_GRID_DATA_AND_SURFACE) {
      LogKit::LogFormatted(LogKit::Low," and snapped to seismic data\n");
      LogKit::LogFormatted(LogKit::Low,"  Grid                                     : "+gridFile+"\n");
    }
  }

  //
  // SURFACES
  //
  LogKit::LogFormatted(LogKit::Low,"\nTime surfaces:\n");
  if (modelSettings->getParallelTimeSurfaces())
  {
    LogKit::LogFormatted(LogKit::Low,"  Reference surface                        : "+inputFiles->getTimeSurfFile(0)+"\n");
    LogKit::LogFormatted(LogKit::Low,"  Shift to top surface                     : %10.1f\n", modelSettings->getTimeDTop());
    LogKit::LogFormatted(LogKit::Low,"  Time slice                               : %10.1f\n", modelSettings->getTimeLz());
    LogKit::LogFormatted(LogKit::Low,"  Sampling density                         : %10.1f\n", modelSettings->getTimeDz());
    LogKit::LogFormatted(LogKit::Low,"  Number of layers                         : %10d\n",   int(modelSettings->getTimeLz()/modelSettings->getTimeDz()+0.5));
  }
  else
  {
    const std::string & topName  = inputFiles->getTimeSurfFile(0);
    const std::string & baseName = inputFiles->getTimeSurfFile(1);

    if (NRLib::IsNumber(topName))
      LogKit::LogFormatted(LogKit::Low,"  Start time                               : %10.2f\n",atof(topName.c_str()));
    else
      LogKit::LogFormatted(LogKit::Low,"  Top surface                              : "+topName+"\n");

    if (NRLib::IsNumber(baseName))
      LogKit::LogFormatted(LogKit::Low,"  Stop time                                : %10.2f\n", atof(baseName.c_str()));
    else
      LogKit::LogFormatted(LogKit::Low,"  Base surface                             : "+baseName+"\n");
      LogKit::LogFormatted(LogKit::Low,"  Number of layers                         : %10d\n", modelSettings->getTimeNz());

    LogKit::LogFormatted(LogKit::Low,"  Minimum allowed value for lmin/lmax      : %10.2f\n", modelSettings->getLzLimit());
  }
  if (inputFiles->getCorrDirFile() != "")
    LogKit::LogFormatted(LogKit::Low,"\n  Correlation direction                    : "+inputFiles->getCorrDirFile()+"\n");

  if (modelSettings->getDoDepthConversion())
  {
    LogKit::LogFormatted(LogKit::Low,"\nDepth conversion:\n");
    if (inputFiles->getDepthSurfFile(0) != "")
      LogKit::LogFormatted(LogKit::Low,"  Top depth surface                        : "+inputFiles->getDepthSurfFile(0)+"\n");
    else
      LogKit::LogFormatted(LogKit::Low,"  Top depth surface                        : %s\n", "Made from base depth surface and velocity field");
    if (inputFiles->getDepthSurfFile(1) != "")
      LogKit::LogFormatted(LogKit::Low,"  Base depth surface                       : "+inputFiles->getDepthSurfFile(1)+"\n");
    else
      LogKit::LogFormatted(LogKit::Low,"  Base depth surface                       : %s\n", "Made from top depth surface and velocity field");
    std::string velocityField = inputFiles->getVelocityField();
    if (modelSettings->getVelocityFromInversion()) {
      velocityField = "Use Vp from inversion";
    }
     LogKit::LogFormatted(LogKit::Low,"  Velocity field                           : "+velocityField+"\n");
  }

  const std::string & topWEI  = inputFiles->getWaveletEstIntFileTop(0);
  const std::string & baseWEI = inputFiles->getWaveletEstIntFileBase(0);

  if (topWEI != "" || baseWEI != "") {
    LogKit::LogFormatted(LogKit::Low,"\nWavelet estimation interval:\n");
    if (NRLib::IsNumber(topWEI))
      LogKit::LogFormatted(LogKit::Low,"  Start time                               : %10.2f\n",atof(topWEI.c_str()));
    else
      LogKit::LogFormatted(LogKit::Low,"  Start time                               : "+topWEI+"\n");

    if (NRLib::IsNumber(baseWEI))
      LogKit::LogFormatted(LogKit::Low,"  Stop time                                : %10.2f\n",atof(baseWEI.c_str()));
    else
      LogKit::LogFormatted(LogKit::Low,"  Stop time                                : "+baseWEI+"\n");
  }

  const std::string & topFEI  = inputFiles->getFaciesEstIntFile(0);
  const std::string & baseFEI = inputFiles->getFaciesEstIntFile(1);

  if (topFEI != "" || baseFEI != "") {
    LogKit::LogFormatted(LogKit::Low,"\nFacies estimation interval:\n");
    if (NRLib::IsNumber(topFEI))
      LogKit::LogFormatted(LogKit::Low,"  Start time                               : %10.2f\n",atof(topFEI.c_str()));
    else
      LogKit::LogFormatted(LogKit::Low,"  Start time                               : "+topFEI+"\n");

    if (NRLib::IsNumber(baseFEI))
      LogKit::LogFormatted(LogKit::Low,"  Stop time                                : %10.2f\n",atof(baseFEI.c_str()));
    else
      LogKit::LogFormatted(LogKit::Low,"  Stop time                                : "+baseFEI+"\n");
  }

  //
  // BACKGROUND
  //
  if (modelSettings->getGenerateBackground())
  {
    LogKit::LogFormatted(LogKit::Low,"\nBackground model (estimated):\n");
    if (inputFiles->getBackVelFile() != "")
      LogKit::LogFormatted(LogKit::Low,"  Trend for p-wave velocity                : "+inputFiles->getBackVelFile()+"\n");
    Vario       * vario  = modelSettings->getBackgroundVario();
    GenExpVario * pVario = dynamic_cast<GenExpVario*>(vario);
    LogKit::LogFormatted(LogKit::Low,"  Variogram\n");
    LogKit::LogFormatted(LogKit::Low,"    Model                                  : %10s\n",(vario->getType()).c_str());
    if (pVario != NULL)
    LogKit::LogFormatted(LogKit::Low,"    Power                                  : %10.1f\n",pVario->getPower());
    LogKit::LogFormatted(LogKit::Low,"    Range                                  : %10.1f\n",vario->getRange());
    if (vario->getAnisotropic())
    {
      LogKit::LogFormatted(LogKit::Low,"    Subrange                               : %10.1f\n",vario->getSubRange());
      LogKit::LogFormatted(LogKit::Low,"    Azimuth                                : %10.1f\n",90.0 - vario->getAngle()*(180/M_PI));
    }
    LogKit::LogFormatted(LogKit::Low,"  High cut frequency for well logs         : %10.1f\n",modelSettings->getMaxHzBackground());
  }
  else
  {
    if(modelSettings->getForwardModeling()==true)
      LogKit::LogFormatted(LogKit::Low,"\nEarth model:\n");
    else
      LogKit::LogFormatted(LogKit::Low,"\nBackground model:\n");

    if (modelSettings->getUseAIBackground()) {
      if (modelSettings->getConstBackValue(0) > 0)
        LogKit::LogFormatted(LogKit::Low,"  Acoustic impedance                       : %10.1f\n",modelSettings->getConstBackValue(0));
      else
        LogKit::LogFormatted(LogKit::Low,"  Acoustic impedance read from file        : "+inputFiles->getBackFile(0)+"\n");
    }
    else {
      if (modelSettings->getConstBackValue(0) > 0)
        LogKit::LogFormatted(LogKit::Low,"  P-wave velocity                          : %10.1f\n",modelSettings->getConstBackValue(0));
      else
        LogKit::LogFormatted(LogKit::Low,"  P-wave velocity read from file           : "+inputFiles->getBackFile(0)+"\n");
    }

    if (modelSettings->getUseSIBackground()) {
      if (modelSettings->getConstBackValue(1) > 0)
        LogKit::LogFormatted(LogKit::Low,"  Shear impedance                          : %10.1f\n",modelSettings->getConstBackValue(1));
      else
        LogKit::LogFormatted(LogKit::Low,"  Shear impedance read from file           : "+inputFiles->getBackFile(1)+"\n");
    }
    else if (modelSettings->getUseVpVsBackground()) {
      if (modelSettings->getConstBackValue(1) > 0)
        LogKit::LogFormatted(LogKit::Low,"  Vp/Vs                                    : %10.1f\n",modelSettings->getConstBackValue(1));
      else
        LogKit::LogFormatted(LogKit::Low,"  Vp/Vs  read from file                    : "+inputFiles->getBackFile(1)+"\n");
    }
    else {
      if (modelSettings->getConstBackValue(1) > 0)
        LogKit::LogFormatted(LogKit::Low,"  S-wave velocity                          : %10.1f\n",modelSettings->getConstBackValue(1));
      else
        LogKit::LogFormatted(LogKit::Low,"  S-wave velocity read from file           : "+inputFiles->getBackFile(1)+"\n");
    }

    if (modelSettings->getConstBackValue(2) > 0)
      LogKit::LogFormatted(LogKit::Low,"  Density                                  : %10.1f\n",modelSettings->getConstBackValue(2));
    else
      LogKit::LogFormatted(LogKit::Low,"  Density read from file                   : "+inputFiles->getBackFile(2)+"\n");
  }

  TraceHeaderFormat * thf_old = modelSettings->getTraceHeaderFormat();
  if (thf_old != NULL)
  {
    LogKit::LogFormatted(LogKit::Low,"\nAdditional SegY trace header format:\n");
    if (thf_old != NULL) {
      LogKit::LogFormatted(LogKit::Low,"  Format name                              : "+thf_old->GetFormatName()+"\n");
      if (thf_old->GetBypassCoordScaling())
        LogKit::LogFormatted(LogKit::Low,"  Bypass coordinate scaling                :        yes\n");
      if (!thf_old->GetStandardType())
      {
        LogKit::LogFormatted(LogKit::Low,"  Start pos coordinate scaling             : %10d\n",thf_old->GetScalCoLoc());
        LogKit::LogFormatted(LogKit::Low,"  Start pos trace x coordinate             : %10d\n",thf_old->GetUtmxLoc());
        LogKit::LogFormatted(LogKit::Low,"  Start pos trace y coordinate             : %10d\n",thf_old->GetUtmyLoc());
        LogKit::LogFormatted(LogKit::Low,"  Start pos inline index                   : %10d\n",thf_old->GetInlineLoc());
        LogKit::LogFormatted(LogKit::Low,"  Start pos crossline index                : %10d\n",thf_old->GetCrosslineLoc());
        LogKit::LogFormatted(LogKit::Low,"  Coordinate system                        : %10s\n",thf_old->GetCoordSys()==0 ? "UTM" : "ILXL" );
      }
    }
  }

  if (modelSettings->getForwardModeling())
  {
    //
    // SEISMIC
    //
    LogKit::LogFormatted(LogKit::Low,"\nGeneral settings for seismic:\n");
    LogKit::LogFormatted(LogKit::Low,"  Generating seismic                       : %10s\n","yes");
    std::vector<float> angle = modelSettings->getAngle(0);
    for (int i = 0 ; i < modelSettings->getNumberOfAngles(0) ; i++) //Forward modeling can only be done for one time lapse
    {
      LogKit::LogFormatted(LogKit::Low,"\nSettings for AVO stack %d:\n",i+1);
      LogKit::LogFormatted(LogKit::Low,"  Angle                                    : %10.1f\n",(angle[i]*180/M_PI));
      LogKit::LogFormatted(LogKit::Low,"  Read wavelet from file                   : "+inputFiles->getWaveletFile(0,i)+"\n");
    }
  }
  else
  {
    //
    // PRIOR CORRELATION
    //
    Vario * corr = modelSettings->getLateralCorr();
    if (corr != NULL) {
      GenExpVario * pCorr = dynamic_cast<GenExpVario*>(corr);
      LogKit::LogFormatted(LogKit::Low,"\nPrior correlation (of residuals):\n");
      LogKit::LogFormatted(LogKit::Low,"  Range of allowed parameter values:\n");
      LogKit::LogFormatted(LogKit::Low,"    Var{Vp}  - min                         : %10.1e\n",modelSettings->getVarAlphaMin());
      LogKit::LogFormatted(LogKit::Low,"    Var{Vp}  - max                         : %10.1e\n",modelSettings->getVarAlphaMax());
      LogKit::LogFormatted(LogKit::Low,"    Var{Vs}  - min                         : %10.1e\n",modelSettings->getVarBetaMin());
      LogKit::LogFormatted(LogKit::Low,"    Var{Vs}  - max                         : %10.1e\n",modelSettings->getVarBetaMax());
      LogKit::LogFormatted(LogKit::Low,"    Var{Rho} - min                         : %10.1e\n",modelSettings->getVarRhoMin());
      LogKit::LogFormatted(LogKit::Low,"    Var{Rho} - max                         : %10.1e\n",modelSettings->getVarRhoMax());
      LogKit::LogFormatted(LogKit::Low,"  Lateral correlation:\n");
      LogKit::LogFormatted(LogKit::Low,"    Model                                  : %10s\n",(corr->getType()).c_str());
      if (pCorr != NULL)
        LogKit::LogFormatted(LogKit::Low,"    Power                                  : %10.1f\n",pCorr->getPower());
      LogKit::LogFormatted(LogKit::Low,"    Range                                  : %10.1f\n",corr->getRange());
      if (corr->getAnisotropic())
      {
        LogKit::LogFormatted(LogKit::Low,"    Subrange                               : %10.1f\n",corr->getSubRange());
        LogKit::LogFormatted(LogKit::Low,"    Azimuth                                : %10.1f\n",90.0 - corr->getAngle()*(180/M_PI));
      }
    }
    //
    // PRIOR FACIES
    //
    if (modelSettings->getIsPriorFaciesProbGiven()==ModelSettings::FACIES_FROM_MODEL_FILE ||
        modelSettings->getIsPriorFaciesProbGiven()==ModelSettings::FACIES_FROM_CUBES)
        // Can not be written when FACIES_FROM_WELLS as this information not is extracted yet
    {
      LogKit::LogFormatted(LogKit::Low,"\nPrior facies probabilities:\n");
      if(modelSettings->getIsPriorFaciesProbGiven()==ModelSettings::FACIES_FROM_MODEL_FILE)
      {
        typedef std::map<std::string,float> mapType;
        mapType myMap = modelSettings->getPriorFaciesProb();

        for(mapType::iterator i=myMap.begin();i!=myMap.end();i++)
          LogKit::LogFormatted(LogKit::Low,"   %-12s                            : %10.2f\n",(i->first).c_str(),i->second);
      }
      else if (modelSettings->getIsPriorFaciesProbGiven()==ModelSettings::FACIES_FROM_CUBES)
      {
        typedef std::map<std::string,std::string> mapType;
        mapType myMap = inputFiles->getPriorFaciesProbFile();

        for(mapType::iterator i=myMap.begin();i!=myMap.end();i++)
          LogKit::LogFormatted(LogKit::Low,"   %-12s                            : %10s\n",(i->first).c_str(),(i->second).c_str());
      }
    }
    //
    // SEISMIC
    //
    if (modelSettings->getNoSeismicNeeded()==false)
    {
      LogKit::LogFormatted(LogKit::Low,"\nGeneral settings for seismic:\n");
      LogKit::LogFormatted(LogKit::Low,"  White noise component                    : %10.2f\n",modelSettings->getWNC());
      LogKit::LogFormatted(LogKit::Low,"  Low cut for inversion                    : %10.1f\n",modelSettings->getLowCut());
      LogKit::LogFormatted(LogKit::Low,"  High cut for inversion                   : %10.1f\n",modelSettings->getHighCut());
      LogKit::LogFormatted(LogKit::Low,"  Guard zone outside interval of interest  : %10.1f ms\n",modelSettings->getGuardZone());
      LogKit::LogFormatted(LogKit::Low,"  Smoothing length in guard zone           : %10.1f ms\n",modelSettings->getSmoothLength());
      LogKit::LogFormatted(LogKit::Low,"  Interpolation threshold                  : %10.1f ms\n",modelSettings->getEnergyThreshold());

      if (modelSettings->getDo4DInversion())
        LogKit::LogFormatted(LogKit::Low,"\n4D seismic data:\n");

      for (int i=0; i<modelSettings->getNumberOfTimeLapses(); i++){
        if(modelSettings->getDo4DInversion())
          LogKit::LogFormatted(LogKit::Low,"\nVintage:\n");
        if(modelSettings->getVintageMonth(i)==IMISSING && modelSettings->getVintageYear(i) != IMISSING)
          LogKit::LogFormatted(LogKit::Low,"    %-2d                                     : %10d\n", i+1, modelSettings->getVintageYear(i));
        else if(modelSettings->getVintageDay(i)==IMISSING && modelSettings->getVintageMonth(i) != IMISSING && modelSettings->getVintageYear(i) != IMISSING)
          LogKit::LogFormatted(LogKit::Low,"    %-2d                                     : %10d %4d\n", i+1, modelSettings->getVintageYear(i), modelSettings->getVintageMonth(i));
        else if(modelSettings->getVintageDay(i)!=IMISSING && modelSettings->getVintageMonth(i)!=IMISSING && modelSettings->getVintageYear(i) != IMISSING)
          LogKit::LogFormatted(LogKit::Low,"    %-2d                                     : %10d %4d %4d\n", i+1, modelSettings->getVintageYear(i), modelSettings->getVintageMonth(i), modelSettings->getVintageDay(i));

      corr  = modelSettings->getAngularCorr(i);
      GenExpVario * pCorr = dynamic_cast<GenExpVario*>(corr);
      LogKit::LogFormatted(LogKit::Low,"  Angular correlation:\n");
      LogKit::LogFormatted(LogKit::Low,"    Model                                  : %10s\n",(corr->getType()).c_str());
      if (pCorr != NULL)
        LogKit::LogFormatted(LogKit::Low,"    Power                                  : %10.1f\n",pCorr->getPower());
      LogKit::LogFormatted(LogKit::Low,"    Range                                  : %10.1f\n",corr->getRange()*180.0/M_PI);
      if (corr->getAnisotropic())
      {
        LogKit::LogFormatted(LogKit::Low,"    Subrange                               : %10.1f\n",corr->getSubRange()*180.0/M_PI);
        LogKit::LogFormatted(LogKit::Low,"    Angle                                  : %10.1f\n",corr->getAngle());
      }
      bool estimateNoise = false;
      for (int j = 0 ; j < modelSettings->getNumberOfAngles(i) ; j++) {
        estimateNoise = estimateNoise || modelSettings->getEstimateSNRatio(i,j);
      }
      LogKit::LogFormatted(LogKit::Low,"\nGeneral settings for wavelet:\n");
      if (estimateNoise)
        LogKit::LogFormatted(LogKit::Low,"  Maximum shift in noise estimation        : %10.1f\n",modelSettings->getMaxWaveletShift());
      LogKit::LogFormatted(LogKit::High,  "  Minimum relative amplitude               : %10.3f\n",modelSettings->getMinRelWaveletAmp());
      LogKit::LogFormatted(LogKit::High,  "  Wavelet tapering length                  : %10.1f\n",modelSettings->getWaveletTaperingL());
      LogKit::LogFormatted(LogKit::High, "  Tuning factor for 3D wavelet estimation  : %10.1f\n", modelSettings->getWavelet3DTuningFactor());
      LogKit::LogFormatted(LogKit::High, "  Smoothing range for gradient (3D wavelet): %10.1f\n", modelSettings->getGradientSmoothingRange());
      LogKit::LogFormatted(LogKit::High, "  Estimate well gradient for seismic data  : %10s\n", (modelSettings->getEstimateWellGradientFromSeismic() ? "yes" : "no"));

      if (modelSettings->getOptimizeWellLocation()) {
        LogKit::LogFormatted(LogKit::Low,"\nGeneral settings for well locations:\n");
        LogKit::LogFormatted(LogKit::Low,"  Maximum offset                           : %10.1f\n",modelSettings->getMaxWellOffset());
        LogKit::LogFormatted(LogKit::Low,"  Maximum vertical shift                   : %10.1f\n",modelSettings->getMaxWellShift());
      }
        std::vector<float> angle = modelSettings->getAngle(i);
        std::vector<float> SNRatio = modelSettings->getSNRatio(i);
        std::vector<bool>  estimateWavelet = modelSettings->getEstimateWavelet(i);
        std::vector<bool>  matchEnergies = modelSettings->getMatchEnergies(i);




        for (int j = 0 ; j < modelSettings->getNumberOfAngles(i) ; j++)
        {
          LogKit::LogFormatted(LogKit::Low,"\nSettings for AVO stack %d:\n",j+1);
          LogKit::LogFormatted(LogKit::Low,"  Angle                                    : %10.1f\n",(angle[j]*180/M_PI));
          LogKit::LogFormatted(LogKit::Low,"  SegY start time                          : %10.1f\n",modelSettings->getSegyOffset(i));
          TraceHeaderFormat * thf = modelSettings->getTraceHeaderFormat(i,j);
          if (thf != NULL)
          {
            LogKit::LogFormatted(LogKit::Low,"  SegY trace header format:\n");
            LogKit::LogFormatted(LogKit::Low,"    Format name                            : "+thf->GetFormatName()+"\n");
            if (thf->GetBypassCoordScaling())
              LogKit::LogFormatted(LogKit::Low,"    Bypass coordinate scaling              :        yes\n");
            if (!thf->GetStandardType())
            {
              LogKit::LogFormatted(LogKit::Low,"    Start pos coordinate scaling           : %10d\n",thf->GetScalCoLoc());
              LogKit::LogFormatted(LogKit::Low,"    Start pos trace x coordinate           : %10d\n",thf->GetUtmxLoc());
              LogKit::LogFormatted(LogKit::Low,"    Start pos trace y coordinate           : %10d\n",thf->GetUtmyLoc());
              LogKit::LogFormatted(LogKit::Low,"    Start pos inline index                 : %10d\n",thf->GetInlineLoc());
              LogKit::LogFormatted(LogKit::Low,"    Start pos crossline index              : %10d\n",thf->GetCrosslineLoc());
              LogKit::LogFormatted(LogKit::Low,"    Coordinate system                      : %10s\n",thf->GetCoordSys()==0 ? "UTM" : "ILXL" );
            }
          }
          LogKit::LogFormatted(LogKit::Low,"  Data                                     : "+inputFiles->getSeismicFile(i,j)+"\n");
          if (estimateWavelet[j])
            LogKit::LogFormatted(LogKit::Low,"  Estimate wavelet                         : %10s\n", "yes");
          else
            LogKit::LogFormatted(LogKit::Low,"  Read wavelet from file                   : "+inputFiles->getWaveletFile(i,j)+"\n");
          if (modelSettings->getEstimateLocalShift(i,j))
           LogKit::LogFormatted(LogKit::Low,"  Estimate local shift map                 : %10s\n", "yes");
          else if (inputFiles->getShiftFile(i,j) != "")
            LogKit::LogFormatted(LogKit::Low,"  Local shift map                          : "+inputFiles->getShiftFile(i,j)+"\n");
          if (modelSettings->getEstimateLocalScale(i,j))
            LogKit::LogFormatted(LogKit::Low,"  Estimate local scale map                 : %10s\n", "yes");
          else if (inputFiles->getScaleFile(i,j) != "")
            LogKit::LogFormatted(LogKit::Low,"  Local scale map                          : "+inputFiles->getScaleFile(i,j)+"\n");
          if (matchEnergies[j])
            LogKit::LogFormatted(LogKit::Low,"  Match empirical and theoretical energies : %10s\n", "yes");
          if (!estimateWavelet[j] && !matchEnergies[j]){
            if (modelSettings->getEstimateGlobalWaveletScale(i,j))
              LogKit::LogFormatted(LogKit::Low,"  Estimate global wavelet scale            : %10s\n","yes");
            else
              LogKit::LogFormatted(LogKit::Low,"  Global wavelet scale                     : %10.2f\n",modelSettings->getWaveletScale(i,j));
          }
          if (modelSettings->getEstimateSNRatio(i,j))
            LogKit::LogFormatted(LogKit::Low,"  Estimate signal-to-noise ratio           : %10s\n", "yes");
          else
            LogKit::LogFormatted(LogKit::Low,"  Signal-to-noise ratio                    : %10.1f\n",SNRatio[j]);
          if (modelSettings->getEstimateLocalNoise(i,j)) {
            if (inputFiles->getLocalNoiseFile(i,j) == "")
              LogKit::LogFormatted(LogKit::Low,"  Estimate local signal-to-noise ratio map : %10s\n", "yes");
            else
              LogKit::LogFormatted(LogKit::Low,"  Local signal-to-noise ratio map          : "+inputFiles->getLocalNoiseFile(i,j)+"\n");
          }
          if (modelSettings->getEstimateLocalNoise(i,j))
            LogKit::LogFormatted(LogKit::Low,"  Estimate local noise                     : %10s\n", "yes");
          if (inputFiles->getLocalNoiseFile(i,j) != "")
            LogKit::LogFormatted(LogKit::Low,"  Local noise                              : "+inputFiles->getLocalNoiseFile(i,j)+"\n");
        }
      }
    }
  }
}

void
ModelGeneral::getCorrGradIJ(float & corrGradI, float &corrGradJ) const
{
  double angle  = timeSimbox_->getAngle();
  double cosrot = cos(angle);
  double sinrot = sin(angle);
  double dx     = timeSimbox_->getdx();
  double dy     = timeSimbox_->getdy();

  double cI =  dx*cosrot*gradX_ + dy*sinrot*gradY_;
  double cJ = -dx*sinrot*gradX_ + dy*cosrot*gradY_;

  corrGradI = float(cI/timeSimbox_->getdz());
  corrGradJ = float(cJ/timeSimbox_->getdz());
}

void
ModelGeneral::processDepthConversion(Simbox            * timeCutSimbox,
                                     Simbox            * timeSimbox,
                                     ModelSettings     * modelSettings,
                                     const InputFiles  * inputFiles,
                                     std::string       & errText,
                                     bool              & failed)
{
  FFTGrid * velocity = NULL;
  if(timeCutSimbox != NULL)
    loadVelocity(velocity, timeCutSimbox, timeCutSimbox, modelSettings,
                 inputFiles->getVelocityField(), velocityFromInversion_,
                 errText, failed);
  else
    loadVelocity(velocity, timeSimbox, timeCutSimbox, modelSettings,
                 inputFiles->getVelocityField(), velocityFromInversion_,
                 errText, failed);

  if(!failed)
  {
    timeDepthMapping_ = new GridMapping();
    timeDepthMapping_->setDepthSurfaces(inputFiles->getDepthSurfFiles(), failed, errText);
    if(velocity != NULL)
    {
      velocity->setAccessMode(FFTGrid::RANDOMACCESS);
      timeDepthMapping_->calculateSurfaceFromVelocity(velocity, timeSimbox);
      timeDepthMapping_->setDepthSimbox(timeSimbox, timeSimbox->getnz(),
                                        modelSettings->getOutputGridFormat(),
                                        failed, errText);            // NBNB-PAL: Er dettet riktig nz (timeCut vs time)?
      timeDepthMapping_->makeTimeDepthMapping(velocity, timeSimbox);
      velocity->endAccess();

      if((modelSettings->getOutputGridsOther() & IO::TIME_TO_DEPTH_VELOCITY) > 0) {
        std::string baseName  = IO::FileTimeToDepthVelocity();
        std::string sgriLabel = std::string("Time-to-depth velocity");
        float       offset    = modelSettings->getSegyOffset(0);//Only allow one segy offset for time lapse data
        velocity->writeFile(baseName,
                            IO::PathToVelocity(),
                            timeSimbox,
                            sgriLabel,
                            offset,
                            timeDepthMapping_,
                            timeCutMapping_);
      }
    }
    else if (velocity==NULL && velocityFromInversion_==false)
    {
      timeDepthMapping_->setDepthSimbox(timeSimbox,
                                        timeSimbox->getnz(),
                                        modelSettings->getOutputGridFormat(),
                                        failed,
                                        errText);

    }
  }
  if(velocity != NULL)
    delete velocity;
}

void ModelGeneral::processRockPhysics(Simbox                       * timeSimbox,
                                      Simbox                       * timeCutSimbox,
                                      ModelSettings                * modelSettings,
                                      bool                         & failed,
                                      std::string                  & errTxt,
                                      const InputFiles             * inputFiles)
{
  if(modelSettings->getFaciesProbFromRockPhysics()){
    std::vector<std::string> trendCubeParameters = modelSettings->getTrendCubeParameters();
    numberOfTrendCubes_ = static_cast<int>(trendCubeParameters.size());
    std::vector<std::string> trendCubeNames(numberOfTrendCubes_);

    std::vector<float> trend_cube_min;
    std::vector<float> trend_cube_max;
    std::vector<std::vector<float> > trend_cube_sampling;

    if(numberOfTrendCubes_ > 0){
      trendCubes_ = new FFTGrid*[numberOfTrendCubes_];
      const SegyGeometry      * dummy1 = NULL;
      const TraceHeaderFormat * dummy2 = NULL;
      const float               offset = modelSettings->getSegyOffset(0); //Facies estimation only allowed for one time lapse

      for(int i=0; i<numberOfTrendCubes_; i++){
        trendCubeNames[i] = inputFiles->getTrendCube(i);
        std::string errorText("");
        ModelGeneral::readGridFromFile(trendCubeNames[i],
                                       "trendcube",
                                       offset,
                                       trendCubes_[i],
                                       dummy1,
                                       dummy2,
                                       FFTGrid::PARAMETER,
                                       timeSimbox,
                                       timeCutSimbox,
                                       modelSettings,
                                       errorText,
                                       true);
        if(errorText != ""){
          errorText += "Reading of file \'"+trendCubeNames[i]+"\' failed\n";
          errTxt += errorText;
          failed = true;
        }

        trendCubes_[i]->calculateStatistics();
        float max = std::ceil(trendCubes_[i]->getMaxReal());
        float min = std::floor(trendCubes_[i]->getMinReal());

        int n_samples = static_cast<int>(max-min+1);
        std::vector<float> sampling(n_samples);

        for(int j=0; j<n_samples; j++)
          sampling[j] = min + j;
        trend_cube_sampling.push_back(sampling);

      }
    }
    std::string path = inputFiles->getInputDirectory();

    for(int i=0; i<modelSettings->getNumberOfRocks(); i++){
      RockPhysicsStorage * rock_physics = modelSettings->getRockPhysicsStorage(i);

      rock_distributions_.push_back(rock_physics->GenerateRockPhysics(path,trendCubeParameters,trend_cube_sampling,errTxt));

    }
    if(errTxt != "")
      failed = true;
  }
}

void
ModelGeneral::loadVelocity(FFTGrid           *& velocity,
                           Simbox             * timeSimbox,
                           Simbox             * timeCutSimbox,
                           ModelSettings      * modelSettings,
                           const std::string  & velocityField,
                           bool               & velocityFromInversion,
                           std::string        & errText,
                           bool               & failed)
{
  LogKit::WriteHeader("Setup time-to-depth relationship");

  if(modelSettings->getVelocityFromInversion() == true)
  {
    velocityFromInversion = true;
    velocity = NULL;
  }
  else if(velocityField == "")
    velocity = NULL;
  else
  {
    const SegyGeometry      * dummy1 = NULL;
    const TraceHeaderFormat * dummy2 = NULL;
    const float               offset = modelSettings->getSegyOffset(0); //Segy offset needs to be the same for all time lapse data
    std::string errorText("");
    readGridFromFile(velocityField,
                     "velocity field",
                     offset,
                     velocity,
                     dummy1,
                     dummy2,
                     FFTGrid::PARAMETER,
                     timeSimbox,
                     timeCutSimbox,
                     modelSettings,
                     errorText);

    if (errorText == "") { // No errors
      //
      // Check that the velocity grid is veldefined.
      //
      float logMin = modelSettings->getAlphaMin();
      float logMax = modelSettings->getAlphaMax();
      const int nzp = velocity->getNzp();
      const int nyp = velocity->getNyp();
      const int nxp = velocity->getNxp();
      const int nz = velocity->getNz();
      const int ny = velocity->getNy();
      const int nx = velocity->getNx();
      int tooLow  = 0;
      int tooHigh = 0;
      velocity->setAccessMode(FFTGrid::READ);
      int rnxp = 2*(nxp/2 + 1);
      for (int k = 0; k < nzp; k++)
        for (int j = 0; j < nyp; j++)
          for (int i = 0; i < rnxp; i++) {
            if(i < nx && j < ny && k < nz) {
              float value = velocity->getNextReal();
              if (value < logMin && value != RMISSING) {
                tooLow++;
              }
              if (value > logMax && value != RMISSING)
                tooHigh++;
            }
          }
      velocity->endAccess();

      if (tooLow+tooHigh > 0) {
        std::string text;
        text += "\nThe velocity grid used as trend in the background model of Vp";
        text += "\ncontains too small and/or too high velocities:";
        text += "\n  Minimum Vp = "+NRLib::ToString(logMin,2)+"    Number of too low values  : "+NRLib::ToString(tooLow);
        text += "\n  Maximum Vp = "+NRLib::ToString(logMax,2)+"    Number of too high values : "+NRLib::ToString(tooHigh);
        text += "\nThe range of allowed values can changed using the ALLOWED_PARAMETER_VALUES keyword\n";
        text += "\naborting...\n";
        errText += "Reading of file '"+velocityField+"' for background velocity field failed.\n";
        errText += text;
        failed = true;
      }
    }
    else {
      errorText += "Reading of file \'"+velocityField+"\' for background velocity field failed.\n";
      errText += errorText;
      failed = true;
    }
  }
}

void
ModelGeneral::writeAreas(const SegyGeometry * areaParams,
                         Simbox             * timeSimbox,
                         std::string        & text)
{
  double areaX0   = areaParams->GetX0();
  double areaY0   = areaParams->GetY0();
  double areaLx   = areaParams->Getlx();
  double areaLy   = areaParams->Getly();
  double areaDx   = areaParams->GetDx();
  double areaDy   = areaParams->GetDy();
  double areaRot  = areaParams->GetAngle();
  double areaXmin = RMISSING;
  double areaXmax = RMISSING;
  double areaYmin = RMISSING;
  double areaYmax = RMISSING;

  findSmallestSurfaceGeometry(areaX0, areaY0, areaLx, areaLy, areaRot,
                              areaXmin, areaYmin, areaXmax, areaYmax);

  LogKit::LogFormatted(LogKit::Low,"\nThe top and/or base time surfaces do not cover the area specified by the "+text);
  LogKit::LogFormatted(LogKit::Low,"\nPlease extrapolate surfaces or specify a smaller AREA in the model file.\n");
  LogKit::LogFormatted(LogKit::Low,"\nArea/resolution           x0           y0            lx        ly     azimuth          dx      dy\n");
  LogKit::LogFormatted(LogKit::Low,"-------------------------------------------------------------------------------------------------\n");
  double azimuth = (-1)*areaRot*(180.0/M_PI);
  if (azimuth < 0)
    azimuth += 360.0;
  LogKit::LogFormatted(LogKit::Low,"Model area       %11.2f  %11.2f    %10.2f %10.2f    %8.3f    %7.2f %7.2f\n\n",
                       areaX0, areaY0, areaLx, areaLy, azimuth, areaDx, areaDy);

  LogKit::LogFormatted(LogKit::Low,"Area                    xmin         xmax           ymin        ymax\n");
  LogKit::LogFormatted(LogKit::Low,"--------------------------------------------------------------------\n");
  LogKit::LogFormatted(LogKit::Low,"%-12s     %11.2f  %11.2f    %11.2f %11.2f\n",
                       text.c_str(),areaXmin, areaXmax, areaYmin, areaYmax);
  const NRLib::Surface<double> & top  = timeSimbox->GetTopSurface();
  const NRLib::Surface<double> & base = timeSimbox->GetBotSurface();
  LogKit::LogFormatted(LogKit::Low,"Top surface      %11.2f  %11.2f    %11.2f %11.2f\n",
                       top.GetXMin(), top.GetXMax(), top.GetYMin(), top.GetYMax());
  LogKit::LogFormatted(LogKit::Low,"Base surface     %11.2f  %11.2f    %11.2f %11.2f\n",
                       base.GetXMin(), base.GetXMax(), base.GetYMin(), base.GetYMax());
}

void
ModelGeneral::findSmallestSurfaceGeometry(const double   x0,
                                          const double   y0,
                                          const double   lx,
                                          const double   ly,
                                          const double   rot,
                                          double       & xMin,
                                          double       & yMin,
                                          double       & xMax,
                                          double       & yMax)
{
  xMin = x0 - ly*sin(rot);
  xMax = x0 + lx*cos(rot);
  yMin = y0;
  yMax = y0 + lx*sin(rot) + ly*cos(rot);
  if (rot < 0) {
    xMin = x0;
    xMax = x0 + lx*cos(rot) - ly*sin(rot);
    yMin = y0 + lx*sin(rot);
    yMax = y0 + ly*cos(rot);
  }
}

void
ModelGeneral::getGeometryFromGridOnFile(const std::string         gridFile,
                                        const TraceHeaderFormat * thf,
                                        SegyGeometry           *& geometry,
                                        std::string             & errText)
{
  geometry = NULL;

  if(gridFile != "") { //May change the condition here, but need geometry if we want to set XL/IL
    int fileType = IO::findGridType(gridFile);
    if(fileType == IO::CRAVA) {
      geometry = geometryFromCravaFile(gridFile);
    }
    else if(fileType == IO::SEGY) {
      try
      {
        geometry = SegY::FindGridGeometry(gridFile, thf);
      }
      catch (NRLib::Exception & e)
      {
        errText = e.what();
      }
    }
    else if(fileType == IO::STORM)
      geometry = geometryFromStormFile(gridFile, errText);
    else if(fileType==IO::SGRI) {
      bool scale = true;
      geometry = geometryFromStormFile(gridFile, errText, scale);
    }
    else {
      errText = "Trying to read grid dimensions from unknown file format.\n";
    }
  }
  else {
    errText = "Cannot get geometry from file. The file name is empty.\n";
  }
}

SegyGeometry *
ModelGeneral::geometryFromCravaFile(const std::string & fileName)
{
  std::ifstream binFile;
  NRLib::OpenRead(binFile, fileName, std::ios::in | std::ios::binary);

  std::string fileType;
  getline(binFile,fileType);

  double x0      = NRLib::ReadBinaryDouble(binFile);
  double y0      = NRLib::ReadBinaryDouble(binFile);
  double dx      = NRLib::ReadBinaryDouble(binFile);
  double dy      = NRLib::ReadBinaryDouble(binFile);
  int    nx      = NRLib::ReadBinaryInt(binFile);
  int    ny      = NRLib::ReadBinaryInt(binFile);
  double IL0     = NRLib::ReadBinaryDouble(binFile);
  double XL0     = NRLib::ReadBinaryDouble(binFile);
  double ilStepX = NRLib::ReadBinaryDouble(binFile);
  double ilStepY = NRLib::ReadBinaryDouble(binFile);
  double xlStepX = NRLib::ReadBinaryDouble(binFile);
  double xlStepY = NRLib::ReadBinaryDouble(binFile);
  double rot     = NRLib::ReadBinaryDouble(binFile);

  binFile.close();

  SegyGeometry * geometry = new SegyGeometry(x0, y0, dx, dy, nx, ny, ///< When XL, IL is available.
                                             IL0, XL0, ilStepX, ilStepY,
                                             xlStepX, xlStepY, rot);
  return(geometry);
}

SegyGeometry *
ModelGeneral::geometryFromStormFile(const std::string & fileName,
                                    std::string       & errText,
                                    bool scale)
{
  SegyGeometry  * geometry  = NULL;
  StormContGrid * stormgrid = NULL;
  std::string     tmpErrText;
  float scalehor;
  if(scale==false)
  {
    scalehor = 1.0;
  }
  else //from sgri file
  {
    LogKit::LogFormatted(LogKit::Low,"Sgri file read. Rescaling z axis from s to ms, x and y from km to m. \n");
    scalehor  = 1000.0;
  }
  try
  {
    stormgrid = new StormContGrid(0,0,0);
    stormgrid->ReadFromFile(fileName);
    stormgrid->SetMissingCode(RMISSING);
  }
  catch (NRLib::Exception & e)
  {
    tmpErrText = e.what();
  }

  if (tmpErrText == "") {
    double x0      = stormgrid->GetXMin()*scalehor;
    double y0      = stormgrid->GetYMin()*scalehor;
    double dx      = stormgrid->GetDX()*scalehor;
    double dy      = stormgrid->GetDY()*scalehor;
    int    nx      = static_cast<int>(stormgrid->GetNI());
    int    ny      = static_cast<int>(stormgrid->GetNJ());
    double rot     = stormgrid->GetAngle();
    double IL0     = 0.0;  ///< Dummy value since information is not contained in format
    double XL0     = 0.0;  ///< Dummy value since information is not contained in format
    double ilStepX =   1;  ///< Dummy value since information is not contained in format
    double ilStepY =   1;  ///< Dummy value since information is not contained in format
    double xlStepX =   1;  ///< Dummy value since information is not contained in format
    double xlStepY =   1;  ///< Dummy value since information is not contained in format
    geometry = new SegyGeometry(x0, y0, dx, dy, nx, ny, ///< When XL, IL is available.
                                IL0, XL0, ilStepX, ilStepY,
                                xlStepX, xlStepY, rot);
  }
  else {
    errText += tmpErrText;
  }

  if (stormgrid != NULL)
    delete stormgrid;

  return(geometry);
}

FFTGrid*
ModelGeneral::createFFTGrid(int nx, int ny, int nz, int nxp, int nyp, int nzp, bool fileGrid)
{
  FFTGrid* fftGrid;

  if(fileGrid)
    fftGrid =  new FFTFileGrid(nx, ny, nz, nxp, nyp, nzp);
  else
    fftGrid =  new FFTGrid(nx, ny, nz, nxp, nyp, nzp);

  return(fftGrid);
}

int
ModelGeneral::computeTime(int year, int month, int day) const
{
  if(year == IMISSING)
    return(0);

  int deltaYear = year-1900; //Ok baseyear.
  int time = 365*deltaYear+deltaYear/4; //Leap years.
  if(month == IMISSING)
    time += 182;
  else {
    std::vector<int> accDays(12,0);
    accDays[1]  = accDays[0]  + 31;
    accDays[2]  = accDays[1]  + 28;
    accDays[3]  = accDays[2]  + 31;
    accDays[4]  = accDays[3]  + 30;
    accDays[5]  = accDays[4]  + 31;
    accDays[6]  = accDays[5]  + 30;
    accDays[7]  = accDays[6]  + 31;
    accDays[8]  = accDays[7]  + 31;
    accDays[9]  = accDays[8]  + 30;
    accDays[10] = accDays[9]  + 31;
    accDays[11] = accDays[10] + 30;

    time += accDays[month];

    if(day == IMISSING)
      time += 15;
    else
      time += day;
  }
  return(time);
}

void
ModelGeneral::generateRockPhysics3DBackground(const std::vector<DistributionsRock *> & rock,
                                              const std::vector<double>              & probability,
                                              const FFTGrid                          & trend1,
                                              const FFTGrid                          & trend2,
                                              FFTGrid                                & vp,
                                              FFTGrid                                & vs,
                                              FFTGrid                                & rho,
                                              double                                 & varVp,
                                              double                                 & varVs,
                                              double                                 & varRho,
                                              double                                 & crossVpVs,
                                              double                                 & crossVpRho,
                                              double                                 & crossVsRho)
{
  // Set up of expectations grids and computation of covariance sums given rock physics.

  // Variables for looping through FFTGrids
  const int nzp = vp.getNzp();
  const int nyp = vp.getNyp();
  const int nz  = vp.getNz();
  const int ny  = vp.getNy();
  const int nx  = vp.getNx();
  const int rnxp = vp.getRNxp();

  const size_t number_of_facies = probability.size();

  // Temporary grids for storing top and base values of (vp,vs,rho) for use in linear interpolation in the padding
  NRLib::Grid2D<float> topVp  (nx, ny, 0.0);
  NRLib::Grid2D<float> topVs  (nx, ny, 0.0);
  NRLib::Grid2D<float> topRho (nx, ny, 0.0);
  NRLib::Grid2D<float> baseVp (nx ,ny, 0.0);
  NRLib::Grid2D<float> baseVs (nx, ny, 0.0);
  NRLib::Grid2D<float> baseRho(nx, ny, 0.0);

  // Local storage for summed combined variances
  NRLib::Grid2D<double> sumVariance(3,3);

  // Loop through all cells in the FFTGrids
  for(int k = 0; k < nzp; k++)
    for(int j = 0; j < nyp; j++)
      for(int i = 0; i < rnxp; i++) {

        // If outside/If in the padding in x- and y-direction, set expectation equal to 0
        if(i >= nx || j >= ny) {
          vp.setNextReal(0.0f);
          vs.setNextReal(0.0f);
          rho.setNextReal(0.0f);
        }

        // If outside in z-direction, use linear interpolation between top and base values of the expectations
        else if(k >= nz) {
          double t  = double(nzp-k+1)/(nzp-nz+1);
          double vpVal =  topVp(i,j)*t  + baseVp(i,j)*(1-t);
          double vsVal =  topVs(i,j)*t  + baseVs(i,j)*(1-t);
          double rhoVal = topRho(i,j)*t + baseRho(i,j)*(1-t);

          // Set interpolated values in expectation grids
          vp.setNextReal(static_cast<float>(vpVal));
          vs.setNextReal(static_cast<float>(vsVal));
          rho.setNextReal(static_cast<float>(rhoVal));
        }

        // Otherwise use trend values to get expectation values for each facies from the rock
        else {
          std::vector<double> trend_params(2);
          trend_params[0] = trend1.getRealValue(i,j,k);
          trend_params[1] = trend2.getRealValue(i,j,k);

          std::vector<float> expectations(3);  // Antar initialisert til 0.
          // Sum up for all facies: probability for a facies multiplied with the expectations of (vp, vs, rho) given the facies
          for(size_t f = 0; f < number_of_facies; f++){
            std::vector<double> m(3);
            m = rock[f]->GetExpectation(trend_params);
            expectations[0] += static_cast<float>(m[0]*probability[f]);
            expectations[1] += static_cast<float>(m[1]*probability[f]);
            expectations[2] += static_cast<float>(m[2]*probability[f]);
          }

          // Set values in expectation grids
          vp.setNextReal(expectations[0]);
          vs.setNextReal(expectations[1]);
          rho.setNextReal(expectations[2]);

          // Store top and base values of the expectations for later use in interpolation in the padded region.
          if(k==0) {
            topVp(i,j)  = expectations[0];
            topVs(i,j)  = expectations[1];
            topRho(i,j) = expectations[2];
          }
          else if(k==nz-1) {
            baseVp(i,j)  = expectations[0];
            baseVs(i,j)  = expectations[1];
            baseRho(i,j) = expectations[2];
          }

          // Compute combined variance for all facies in the given grid cell.
          // Calculation of variance for a set of pdfs with given probability in the rock physics model:
          //
          // Var(X) = E([X-E(X)]^2) = E([X - E(X|facies)]^2) + E([E(X|facies) -E(X)]^2) = E(Var(X|facies)) + Var(E(X|facies))
          //        = Sum_{over all facies} (probability of facies * variance given facies) + sum_{over all facies} probability of facies * (expected value given facies - EX)^2,
          // where EX is the sum of probability of a facies multiplied with expectation of \mu given facies

          // For all facies: Summing up expected value of variances and variance of expected values
          for(size_t f = 0; f < number_of_facies; f++){
            NRLib::Grid2D<double> sigma;
            std::vector<double> m(3);
            sigma = rock[f]->GetCovariance(trend_params);
            m = rock[f]->GetExpectation(trend_params);

            // For all elements in the 3x3 matrix of the combined variance
            for(size_t a=0; a<sigma.GetNI(); a++){
              for(size_t b=0; b<sigma.GetNJ(); b++){
                sumVariance(a,b) += probability[f]*sigma(a,b);
                sumVariance(a,b) += probability[f]*(m[a] - expectations[a])*(m[b] - expectations[b]);
              }
            }
          }
        }
      }

      // Setting output variables
      varVp  = sumVariance(0,0);
      varVs  = sumVariance(1,1);
      varRho = sumVariance(2,2);
      crossVpVs  = sumVariance(0,1);
      crossVpRho = sumVariance(0,2);
      crossVsRho = sumVariance(1,2);

}

void
ModelGeneral::generateRockPhysics4DBackground(const std::vector<DistributionsRock *> & rock,
                                              const std::vector<double>              & probability,
                                              int                                      lowCut,
                                              const FFTGrid                          & trend1,
                                              const FFTGrid                          & trend2,
                                              Corr                                   & correlations, //The grids here get/set correctly.
                                              const Simbox                           & timeSimbox,
                                              const ModelSettings                    & modelSettings,
                                              State4D                                & state4d)
{
  // Create all necessary grids for 4D inversion and return all grids in a State4D object.
  // We assume an existing object of the class Corr and an empty object of state4d.

  // Static mu
  FFTGrid * vp_stat;
  FFTGrid * vs_stat;
  FFTGrid * rho_stat;

  // Static sigma
  FFTGrid * vp_vp_stat;
  FFTGrid * vp_vs_stat;
  FFTGrid * vp_rho_stat;
  FFTGrid * vs_vs_stat;
  FFTGrid * vs_rho_stat;
  FFTGrid * rho_rho_stat;

  // The dynamic grids are NULL

  // Variance coefficients which will be set in generateRockPhysics3DBackground
  double varVp;
  double varVs;
  double varRho;
  double crVpVs;
  double crVpRho;
  double crVsRho;

  // Parameters for generating new FFTGrids
  const int nx    = timeSimbox.getnx();
  const int ny    = timeSimbox.getny();
  const int nz    = timeSimbox.getnz();
  const int nxPad = modelSettings.getNXpad();
  const int nyPad = modelSettings.getNYpad();
  const int nzPad = modelSettings.getNZpad();

  // Creating grids for mu static
  vp_stat  = ModelGeneral::createFFTGrid(nx, ny, nz, nxPad, nyPad, nzPad, modelSettings.getFileGrid());
  vs_stat  = ModelGeneral::createFFTGrid(nx, ny, nz, nxPad, nyPad, nzPad, modelSettings.getFileGrid());
  rho_stat = ModelGeneral::createFFTGrid(nx, ny, nz, nxPad, nyPad, nzPad, modelSettings.getFileGrid());

  vp_stat ->createRealGrid();
  vs_stat ->createRealGrid();
  rho_stat->createRealGrid();

  // Creating grids for sigma static
  vp_vp_stat   = ModelGeneral::createFFTGrid(nx, ny, nz, nxPad, nyPad, nzPad, modelSettings.getFileGrid());
  vp_vs_stat   = ModelGeneral::createFFTGrid(nx, ny, nz, nxPad, nyPad, nzPad, modelSettings.getFileGrid());
  vp_rho_stat  = ModelGeneral::createFFTGrid(nx, ny, nz, nxPad, nyPad, nzPad, modelSettings.getFileGrid());
  vs_vs_stat   = ModelGeneral::createFFTGrid(nx, ny, nz, nxPad, nyPad, nzPad, modelSettings.getFileGrid());
  vs_rho_stat  = ModelGeneral::createFFTGrid(nx, ny, nz, nxPad, nyPad, nzPad, modelSettings.getFileGrid());
  rho_rho_stat = ModelGeneral::createFFTGrid(nx, ny, nz, nxPad, nyPad, nzPad, modelSettings.getFileGrid());

  vp_vp_stat  ->createRealGrid();
  vp_vs_stat  ->createRealGrid();
  vp_rho_stat ->createRealGrid();
  vs_vs_stat  ->createRealGrid();
  vs_rho_stat ->createRealGrid();
  rho_rho_stat->createRealGrid();

  // For the static variables, generate expectation grids and variance coefficients from the 3D settings.
  ModelGeneral::generateRockPhysics3DBackground(rock, probability, trend1, trend2, *vp_stat, *vs_stat, *rho_stat, varVp, varVs, varRho, crVpVs, crVpRho, crVsRho);

  // Correlations
  float corrGradI, corrGradJ;
  ModelGeneral::getCorrGradIJ(corrGradI, corrGradJ);
  vp_vp_stat  ->fillInParamCorr(&correlations, lowCut, corrGradI, corrGradJ);
  vp_vs_stat  ->fillInParamCorr(&correlations, lowCut, corrGradI, corrGradJ);
  vp_rho_stat ->fillInParamCorr(&correlations, lowCut, corrGradI, corrGradJ);
  vs_vs_stat  ->fillInParamCorr(&correlations, lowCut, corrGradI, corrGradJ);
  vs_rho_stat ->fillInParamCorr(&correlations, lowCut, corrGradI, corrGradJ);
  rho_rho_stat->fillInParamCorr(&correlations, lowCut, corrGradI, corrGradJ);

  // Multiply covariance grids with scalar variance coefficients
  vp_vp_stat  ->multiplyByScalar(static_cast<float>(varVp));
  vp_vs_stat  ->multiplyByScalar(static_cast<float>(crVpVs));
  vp_rho_stat ->multiplyByScalar(static_cast<float>(crVpRho));
  vs_vs_stat  ->multiplyByScalar(static_cast<float>(varVs));
  vs_rho_stat ->multiplyByScalar(static_cast<float>(crVsRho));
  rho_rho_stat->multiplyByScalar(static_cast<float>(varRho));

  // Set the static and dynamic grids in the state4d object
  state4d.SetStaticMu(vp_stat, vs_stat, rho_stat);
  state4d.SetStaticSigma(vp_vp_stat, vp_vs_stat, vp_rho_stat, vs_vs_stat, vs_rho_stat, rho_rho_stat);

  // Not necessary to set dynamic grids, as all grids are initially initialized as NULL in State4D.
  //state4d.SetDynamicMu(NULL, NULL, NULL);
  //state4d.SetDynamicSigma(NULL, NULL, NULL, NULL, NULL, NULL);
  //state4d.SetStaticDynamicSigma(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

}

void
ModelGeneral::processWells(std::vector<WellData *> & wells,
                             Simbox              * timeSimbox,
                             ModelSettings      *& modelSettings,
                             const InputFiles    * inputFiles,
                             std::string         & errText,
                             bool                & failed)
{
  int     nWells         = modelSettings->getNumberOfWells();

  if(nWells > 0) {

    double wall=0.0, cpu=0.0;
    TimeKit::getTime(wall,cpu);

    LogKit::WriteHeader("Reading and processing wells");

    bool    faciesLogGiven = modelSettings->getFaciesLogGiven();
    int     nFacies        = 0;
    int     error = 0;

    std::string tmpErrText("");
    wells.resize(nWells);
    for(int i=0 ; i<nWells ; i++) {
      wells[i] = new WellData(inputFiles->getWellFile(i),
        modelSettings->getLogNames(),
        modelSettings->getInverseVelocity(),
        modelSettings,
        modelSettings->getIndicatorFacies(i),
        modelSettings->getIndicatorFilter(i),
        modelSettings->getIndicatorWavelet(i),
        modelSettings->getIndicatorBGTrend(i),
        modelSettings->getIndicatorRealVs(i),
        faciesLogGiven);
      if(wells[i]->checkError(tmpErrText) != 0) {
        errText += tmpErrText;
        error = 1;
      }
    }


    if (error == 0) {
      if(modelSettings->getFaciesLogGiven()) {
        setFaciesNames(wells, modelSettings, tmpErrText, error);
        nFacies = modelSettings->getNumberOfFacies(); // nFacies is set in setFaciesNames()
      }
      if (error>0)
        errText += "Prior facies probabilities failed.\n"+tmpErrText;

      int   * validWells    = new int[nWells];
      bool  * validIndex    = new bool[nWells];
      int   * nMerges       = new int[nWells];
      int   * nInvalidAlpha = new int[nWells];
      int   * nInvalidBeta  = new int[nWells];
      int   * nInvalidRho   = new int[nWells];
      float * rankCorr      = new float[nWells];
      float * devAngle      = new float[nWells];
      int  ** faciesCount   = NULL;

      if(nFacies > 0) {
        faciesCount = new int * [nWells];
        for (int i = 0 ; i < nWells ; i++)
          faciesCount[i] = new int[nFacies];
      }

      int count = 0;
      int nohit=0;
      int empty=0;
      int facieslognotok = 0;
      int upwards=0;
      LogKit::LogFormatted(LogKit::Low,"\n");
      for (int i=0 ; i<nWells ; i++)
      {
        bool skip = false;
        LogKit::LogFormatted(LogKit::Low,wells[i]->getWellname()+" : \n");
        if(wells[i]!=NULL) {
          if(wells[i]->checkSimbox(timeSimbox) == 1) {
            skip = true;
            nohit++;
            TaskList::addTask("Consider increasing the inversion volume such that well "+wells[i]->getWellname()+ " can be included");
          }
          if(wells[i]->getNd() == 0) {
            LogKit::LogFormatted(LogKit::Low,"  IGNORED (no log entries found)\n");
            skip = true;
            empty++;
            TaskList::addTask("Check the log entries in well "+wells[i]->getWellname()+".");
          }
          if(wells[i]->isFaciesOk()==0) {
            LogKit::LogFormatted(LogKit::Low,"   IGNORED (facies log has wrong entries)\n");
            skip = true;
            facieslognotok++;
            TaskList::addTask("Check the facies logs in well "+wells[i]->getWellname()+".\n       The facies logs in this well are wrong and the well is ignored");
          }
          if(wells[i]->removeDuplicateLogEntries(timeSimbox, nMerges[i]) == false) {
            LogKit::LogFormatted(LogKit::Low,"   IGNORED (well is too far from monotonous in time)\n");
            skip = true;
            upwards++;
            TaskList::addTask("Check the TWT log in well "+wells[i]->getWellname()+".\n       The well is moving too much upwards, and the well is ignored");
          }
          if(skip)
            validIndex[i] = false;
          else {
            validIndex[i] = true;
            wells[i]->setWrongLogEntriesUndefined(nInvalidAlpha[i], nInvalidBeta[i], nInvalidRho[i]);
            wells[i]->filterLogs();
            //wells[i]->findMeanVsVp(waveletEstimInterval_);
            wells[i]->lookForSyntheticVsLog(rankCorr[i]);
            wells[i]->calculateDeviation(devAngle[i], timeSimbox);

            if (nFacies > 0)
              wells[i]->countFacies(timeSimbox,faciesCount[i]);
            validWells[count] = i;
            count++;
          }
        }
      }
      //
      // Write summary.
      //
      LogKit::LogFormatted(LogKit::Low,"\n");
      LogKit::LogFormatted(LogKit::Low,"                                      Invalid                                    \n");
      LogKit::LogFormatted(LogKit::Low,"Well                    Merges      Vp   Vs  Rho  synthVs/Corr    Deviated/Angle \n");
      LogKit::LogFormatted(LogKit::Low,"---------------------------------------------------------------------------------\n");
      for(int i=0 ; i<nWells ; i++) {
        if (validIndex[i])
          LogKit::LogFormatted(LogKit::Low,"%-23s %6d    %4d %4d %4d     %3s / %5.3f      %3s / %4.1f\n",
          wells[i]->getWellname().c_str(),
          nMerges[i],
          nInvalidAlpha[i],
          nInvalidBeta[i],
          nInvalidRho[i],
          (wells[i]->hasSyntheticVsLog() ? "yes" : " no"),
          rankCorr[i],
          (devAngle[i] > modelSettings->getMaxDevAngle() ? "yes" : " no"),
          devAngle[i]);
        else
          LogKit::LogFormatted(LogKit::Low,"%-23s      -       -    -    -       - /     -       -  /    -\n",
          wells[i]->getWellname().c_str());
      }

      //
      // Print facies count for each well
      //
      if(nFacies > 0) {
        //
        // Probabilities
        //
        LogKit::LogFormatted(LogKit::Low,"\nFacies distributions for each well: \n");
        LogKit::LogFormatted(LogKit::Low,"\nWell                    ");
        for (int i = 0 ; i < nFacies ; i++)
          LogKit::LogFormatted(LogKit::Low,"%12s ",modelSettings->getFaciesName(i).c_str());
        LogKit::LogFormatted(LogKit::Low,"\n");
        for (int i = 0 ; i < 24+13*nFacies ; i++)
          LogKit::LogFormatted(LogKit::Low,"-");
        LogKit::LogFormatted(LogKit::Low,"\n");
        for (int i = 0 ; i < nWells ; i++) {
          if (validIndex[i]) {
            float tot = 0.0;
            for (int f = 0 ; f < nFacies ; f++)
              tot += static_cast<float>(faciesCount[i][f]);
            LogKit::LogFormatted(LogKit::Low,"%-23s ",wells[i]->getWellname().c_str());
            for (int f = 0 ; f < nFacies ; f++) {
              if (tot > 0) {
                float faciesProb = static_cast<float>(faciesCount[i][f])/tot;
                LogKit::LogFormatted(LogKit::Low,"%12.4f ",faciesProb);
              }
              else
                LogKit::LogFormatted(LogKit::Low,"         -   ");
            }
            LogKit::LogFormatted(LogKit::Low,"\n");
          }
          else {
            LogKit::LogFormatted(LogKit::Low,"%-23s ",wells[i]->getWellname().c_str());
            for (int f = 0 ; f < nFacies ; f++)
              LogKit::LogFormatted(LogKit::Low,"         -   ");
            LogKit::LogFormatted(LogKit::Low,"\n");

          }
        }
        LogKit::LogFormatted(LogKit::Low,"\n");
        //
        // Counts
        //
        LogKit::LogFormatted(LogKit::Medium,"\nFacies counts for each well: \n");
        LogKit::LogFormatted(LogKit::Medium,"\nWell                    ");
        for (int i = 0 ; i < nFacies ; i++)
          LogKit::LogFormatted(LogKit::Medium,"%12s ",modelSettings->getFaciesName(i).c_str());
        LogKit::LogFormatted(LogKit::Medium,"\n");
        for (int i = 0 ; i < 24+13*nFacies ; i++)
          LogKit::LogFormatted(LogKit::Medium,"-");
        LogKit::LogFormatted(LogKit::Medium,"\n");
        for (int i = 0 ; i < nWells ; i++) {
          if (validIndex[i]) {
            float tot = 0.0;
            for (int f = 0 ; f < nFacies ; f++)
              tot += static_cast<float>(faciesCount[i][f]);
            LogKit::LogFormatted(LogKit::Medium,"%-23s ",wells[i]->getWellname().c_str());
            for (int f = 0 ; f < nFacies ; f++) {
              LogKit::LogFormatted(LogKit::Medium,"%12d ",faciesCount[i][f]);
            }
            LogKit::LogFormatted(LogKit::Medium,"\n");
          }
          else {
            LogKit::LogFormatted(LogKit::Medium,"%-23s ",wells[i]->getWellname().c_str());
            for (int f = 0 ; f < nFacies ; f++)
              LogKit::LogFormatted(LogKit::Medium,"         -   ");
            LogKit::LogFormatted(LogKit::Medium,"\n");

          }
        }
        LogKit::LogFormatted(LogKit::Medium,"\n");
      }

      //
      // Remove invalid wells
      //
      for(int i=0 ; i<nWells ; i++)
        if (!validIndex[i])
          delete wells[i];
      for(int i=0 ; i<count ; i++)
        wells[i] = wells[validWells[i]];
      for(int i=count ; i<nWells ; i++)
        wells[i] = NULL;
      nWells = count;
      modelSettings->setNumberOfWells(nWells);

      delete [] validWells;
      delete [] validIndex;
      delete [] nMerges;
      delete [] nInvalidAlpha;
      delete [] nInvalidBeta;
      delete [] nInvalidRho;
      delete [] rankCorr;
      delete [] devAngle;

      if (nohit>0)
        LogKit::LogFormatted(LogKit::Low,"\nWARNING: %d well(s) do not hit the inversion volume and will be ignored.\n",nohit);
      if (empty>0)
        LogKit::LogFormatted(LogKit::Low,"\nWARNING: %d well(s) contain no log entries and will be ignored.\n",empty);
      if(facieslognotok>0)
        LogKit::LogFormatted(LogKit::Low,"\nWARNING: %d well(s) have wrong facies logs and will be ignored.\n",facieslognotok);
      if(upwards>0)
        LogKit::LogFormatted(LogKit::Low,"\nWARNING: %d well(s) are moving upwards in TWT and will be ignored.\n",upwards);
      if (nWells==0 && modelSettings->getNoWellNedded()==false) {
        LogKit::LogFormatted(LogKit::Low,"\nERROR: There are no wells left for data analysis. Please check that the inversion area given");
        LogKit::LogFormatted(LogKit::Low,"\n       below is correct. If it is not, you probably have problems with coordinate scaling.");
        LogKit::LogFormatted(LogKit::Low,"\n                                   X0          Y0        DeltaX      DeltaY      Angle");
        LogKit::LogFormatted(LogKit::Low,"\n       -------------------------------------------------------------------------------");
        LogKit::LogFormatted(LogKit::Low,"\n       Inversion area:    %11.2f %11.2f   %11.2f %11.2f   %8.3f\n",
          timeSimbox->getx0(), timeSimbox->gety0(),
          timeSimbox->getlx(), timeSimbox->getly(),
          (timeSimbox->getAngle()*180)/M_PI);
        errText += "No wells available for estimation.";
        error = 1;
      }

      if(nFacies > 0) {
        int fc;
        for(int i = 0; i < nFacies; i++){
          fc = 0;
          for(int j = 0; j < nWells; j++){
            fc+=faciesCount[j][i];
          }
          if(fc == 0){
            LogKit::LogFormatted(LogKit::Low,"\nWARNING: Facies %s is not observed in any of the wells, and posterior facies probability can not be estimated for this facies.\n",modelSettings->getFaciesName(i).c_str() );
            TaskList::addTask("In order to estimate prior facies probability for facies "+ modelSettings->getFaciesName(i) + " add wells which contain observations of this facies.\n");
          }
        }
        for (int i = 0 ; i<nWells ; i++)
          delete [] faciesCount[i];
        delete [] faciesCount;
      }

    }
    failed = error > 0;
    Timings::setTimeWells(wall,cpu);
  }
}

void
ModelGeneral::setFaciesNames(std::vector<WellData *>     wells,
                             ModelSettings            *& modelSettings,
                             std::string               & tmpErrText,
                             int                       & error)
{
  int min,max;
  int globalmin = 0;
  int globalmax = 0;
  bool first = true;
  for (int w = 0; w < modelSettings->getNumberOfWells(); w++) {
    if(wells[w]->isFaciesLogDefined())
    {
      wells[w]->getMinMaxFnr(min,max);
      if(first==true)
      {
        globalmin = min;
        globalmax = max;
        first = false;
      }
      else
      {
        if(min<globalmin)
          globalmin = min;
        if(max>globalmax)
          globalmax = max;
      }
    }
  }

  int nnames = globalmax - globalmin + 1;
  std::vector<std::string> names(nnames);

  for(int w=0 ; w<modelSettings->getNumberOfWells() ; w++)
  {
    if(wells[w]->isFaciesLogDefined())
    {
      for(int i=0 ; i < wells[w]->getNFacies() ; i++)
      {
        std::string name = wells[w]->getFaciesName(i);
        int         fnr  = wells[w]->getFaciesNr(i) - globalmin;

        if(names[fnr] == "") {
          names[fnr] = name;
        }
        else if(names[fnr] != name)
        {
          tmpErrText += "Problem with facies logs. Facies names and numbers are not uniquely defined.\n";
          error++;
        }
      }
    }
  }

  LogKit::LogFormatted(LogKit::Low,"\nFaciesLabel      FaciesName           ");
  LogKit::LogFormatted(LogKit::Low,"\n--------------------------------------\n");
  for(int i=0 ; i<nnames ; i++)
    if(names[i] != "")
      LogKit::LogFormatted(LogKit::Low,"    %2d           %-20s\n",i+globalmin,names[i].c_str());

  int nFacies = 0;
  for(int i=0 ; i<nnames ; i++)
    if(names[i] != "")
      nFacies++;

  for(int i=0 ; i<nnames ; i++)
  {
    if(names[i] != "")
    {
      modelSettings->addFaciesName(names[i]);
      modelSettings->addFaciesLabel(globalmin + i);
    }
  }
}

void
ModelGeneral::processWellLocation(FFTGrid                       ** seisCube,
                                    float                       ** reflectionMatrix,
                                    ModelSettings                * modelSettings,
                                    const std::vector<Surface *> & interval)
{
  LogKit::WriteHeader("Estimating optimized well location");

  double  deltaX, deltaY;
  float   sum;
  float   kMove;
  float   moveAngle;
  int     iMove;
  int     jMove;
  int     i,j,w;
  int     iMaxOffset;
  int     jMaxOffset;
  int     nMoveAngles = 0;
  int     nWells      = modelSettings->getNumberOfWells();
  int     nAngles     = modelSettings->getNumberOfAngles(0);//Well location is not estimated when using time lapse data
  float   maxShift    = modelSettings->getMaxWellShift();
  float   maxOffset   = modelSettings->getMaxWellOffset();
  double  angle       = timeSimbox_->getAngle();
  double  dx          = timeSimbox_->getdx();
  double  dy          = timeSimbox_->getdx();
  std::vector<float> seismicAngle = modelSettings->getAngle(0); //Use first time lapse as this not is allowed in 4D

  std::vector<float> angleWeight(nAngles);
  LogKit::LogFormatted(LogKit::Low,"\n");
  LogKit::LogFormatted(LogKit::Low,"  Well             Shift[ms]       DeltaI   DeltaX[m]   DeltaJ   DeltaY[m] \n");
  LogKit::LogFormatted(LogKit::Low,"  ----------------------------------------------------------------------------------\n");

  for (w = 0 ; w < nWells ; w++) {
    if( wells_[w]->isDeviated()==true )
      continue;

    BlockedLogs * bl = wells_[w]->getBlockedLogsOrigThick();
    nMoveAngles = modelSettings->getNumberOfWellAngles(w);

    if( nMoveAngles==0 )
      continue;

    for( i=0; i<nAngles; i++ )
      angleWeight[i] = 0;

    for( i=0; i<nMoveAngles; i++ ){
      moveAngle   = modelSettings->getWellMoveAngle(w,i);

      for( j=0; j<nAngles; j++ ){
        if( moveAngle == seismicAngle[j]){
          angleWeight[j] = modelSettings->getWellMoveWeight(w,i);
          break;
        }
      }
    }

    sum = 0;
    for( i=0; i<nAngles; i++ )
      sum += angleWeight[i];
    if( sum == 0 )
      continue;

    iMaxOffset = static_cast<int>(std::ceil(maxOffset/dx));
    jMaxOffset = static_cast<int>(std::ceil(maxOffset/dy));

    bl->findOptimalWellLocation(seisCube,timeSimbox_,reflectionMatrix,nAngles,angleWeight,maxShift,iMaxOffset,jMaxOffset,interval,iMove,jMove,kMove);

    deltaX = iMove*dx*cos(angle) - jMove*dy*sin(angle);
    deltaY = iMove*dx*sin(angle) + jMove*dy*cos(angle);
    wells_[w]->moveWell(timeSimbox_,deltaX,deltaY,kMove);
    wells_[w]->deleteBlockedLogsOrigThick();
    wells_[w]->setBlockedLogsOrigThick( new BlockedLogs(wells_[w], timeSimbox_, modelSettings->getRunFromPanel()) );
    LogKit::LogFormatted(LogKit::Low,"  %-13s %11.2f %12d %11.2f %8d %11.2f \n",
    wells_[w]->getWellname().c_str(), kMove, iMove, deltaX, jMove, deltaY);
  }

   for (w = 0 ; w < nWells ; w++){
     nMoveAngles = modelSettings->getNumberOfWellAngles(w);

    if( wells_[w]->isDeviated()==true && nMoveAngles > 0 )
    {
      LogKit::LogFormatted(LogKit::Warning,"\nWARNING: Well %7s is treated as deviated and can not be moved.\n",
          wells_[w]->getWellname().c_str());
      TaskList::addTask("Well "+NRLib::ToString(wells_[w]->getWellname())+" can not be moved. Remove <optimize-location-to> for this well");
    }
   }
}
void
ModelGeneral::processPriorCorrelations(Corr                   *& correlations,
                                       Background              * background,
                                       std::vector<WellData *>   wells,
                                       Simbox                  * timeSimbox,
                                       ModelSettings           * modelSettings,
                                       FFTGrid                ** seisCube,
                                       const InputFiles        * inputFiles,
                                       std::string             & errText,
                                       bool                    & failed)
{
  bool printResult = ((modelSettings->getOtherOutputFlag() & IO::PRIORCORRELATIONS) > 0 ||
                      modelSettings->getEstimationMode() == true);
  if (modelSettings->getDoInversion() || printResult)
  {
    LogKit::WriteHeader("Prior Covariance");

    double wall=0.0, cpu=0.0;
    TimeKit::getTime(wall,cpu);

    const std::string & paramCovFile = inputFiles->getParamCorrFile();
    const std::string & corrTFile    = inputFiles->getTempCorrFile();

    bool estimateParamCov = paramCovFile == "";
    bool estimateTempCorr = corrTFile    == "";

    //
    // Read parameter covariance (Var0) from file
    //
    float ** paramCorr = NULL;
    bool failedParamCorr = false;
    if(!estimateParamCov)
    {
      std::string tmpErrText("");
      paramCorr = ModelAVODynamic::readMatrix(paramCovFile, 3, 3, "parameter covariance", tmpErrText);
      if(paramCorr == NULL)
      {
        errText += "Reading of file "+paramCovFile+" for parameter covariance matrix failed\n";
        errText += tmpErrText;
        failedParamCorr = true;
      }
    }

    //
    // Estimate lateral correlation from seismic data
    //
    Surface * CorrXY = findCorrXYGrid(timeSimbox, modelSettings);

    if(modelSettings->getLateralCorr()==NULL) // NBNB-PAL: this will never be true (default lateral corr)
    {
      int timelapse = 0; // Setting timelapse = 0 as this is the generation of prior model
      estimateCorrXYFromSeismic(CorrXY, seisCube, modelSettings->getNumberOfAngles(timelapse));
    }

    int nCorrT = modelSettings->getNZpad();
    if((nCorrT % 2) == 0)
      nCorrT = nCorrT/2+1;
    else
      nCorrT = nCorrT/2;
    float * corrT = NULL;

    bool failedTempCorr = false;
    if(!estimateTempCorr)
    {
      std::string tmpErrText("");
      float ** corrMat = ModelAVODynamic::readMatrix(corrTFile, 1, nCorrT+1, "temporal correlation", tmpErrText);
      if(corrMat == NULL)
      {
        errText += "Reading of file '"+corrTFile+"' for temporal correlation failed\n";
        errText += tmpErrText;
        failedTempCorr = true;
      }
      corrT = new float[nCorrT];
      if (!failedTempCorr)
      {
        for(int i=0;i<nCorrT;i++)
          corrT[i] = corrMat[0][i+1];
        delete [] corrMat[0];
        delete [] corrMat;
      }
    }

    float ** pointVar0 = NULL;
    if (estimateParamCov || estimateTempCorr) //Need well estimation
    {
      std::string tmpErrTxt;
      Analyzelog * analyze = new Analyzelog(wells,
                                            background,
                                            timeSimbox,
                                            modelSettings,
                                            tmpErrTxt);
      if (tmpErrTxt != "") {
        errText += tmpErrTxt;
        failedParamCorr = true;
      }

      if(estimateParamCov)
        paramCorr = analyze->getVar0();
      else
        delete [] analyze->getVar0();

      pointVar0 = analyze->getPointVar0();

      float * estCorrT = analyze->getCorrT();
      if(estimateTempCorr) {
        corrT = new float[nCorrT];
        int nEst = analyze->getNumberOfLags();
        int i, max = nEst;
        if(max > nCorrT)
          max = nCorrT;
        for(i=0;i<max;i++)
          corrT[i] = estCorrT[i];
        if(i<nCorrT) {
          LogKit::LogFormatted(LogKit::High,
            "\nOnly able to estimate %d of %d lags needed in temporal correlation. The rest are set to 0.\n", nEst, nCorrT);
          for(;i<nCorrT;i++)
            corrT[i] = 0.0f;
        }
      }
      delete [] estCorrT;

      delete analyze;
    }

    if (failedParamCorr || failedTempCorr)
      failed = true;

    if (!failed) {
      correlations = new Corr(pointVar0,
                              paramCorr,
                              corrT,
                              nCorrT,
                              static_cast<float>(timeSimbox->getdz()),
                              CorrXY);
      if(printResult)
        correlations->writeFilePriorVariances(modelSettings);
      correlations->printPriorVariances();
    }

    if(failedTempCorr == false && failedParamCorr == false && correlations == NULL)
    {
      errText += "Could not construct prior covariance. Unknown why...\n";
      failed = true;
    }

    Timings::setTimePriorCorrelation(wall,cpu);
  }
}
Surface *
ModelGeneral::findCorrXYGrid(Simbox * timeSimbox, ModelSettings * modelSettings)
{
  float dx  = static_cast<float>(timeSimbox->getdx());
  float dy  = static_cast<float>(timeSimbox->getdy());

  int   nx  = modelSettings->getNXpad();
  int   ny  = modelSettings->getNYpad();

  Surface * grid = new Surface(0, 0, dx*nx, dy*ny, nx, ny, RMISSING);

  if(modelSettings->getLateralCorr()!=NULL) // NBNB-PAL: Denne her blir aldri null etter at jeg la inn en default lateral correlation i modelsettings.
  {
    int refi,refj;
    for(int j=0;j<ny;j++)
    {
      for(int i=0;i<nx;i++)
      {
        if(i<(nx/2+1))
        {
          refi = i;
        }
        else
        {
          refi = i-nx;
        }
        if(j< (ny/2+1))
        {
          refj = j;
        }
        else
        {
          refj = j-ny;
        }
        (*grid)(j*nx+i) = modelSettings->getLateralCorr()->corr(refi*dx, refj*dy);
      }
    }
  }
  return(grid);
}

void
ModelGeneral::estimateCorrXYFromSeismic(Surface *& corrXY,
                                        FFTGrid ** seisCube,
                                        int numberOfAngles)
{
  FFTGrid * transf;
  float   * grid;

  int n = static_cast<int>(corrXY->GetNI()*corrXY->GetNJ());
  grid = new float[n];

  for(int i=0 ; i<n ; i++)
    grid[i] = 0.0;

  for(int i=0 ; i<numberOfAngles ; i++)
  {
    if(seisCube[i]->isFile())
      transf = new FFTFileGrid(static_cast<FFTFileGrid *>(seisCube[i])); //move new out of loop? Copy grid instead
    else
      transf = new FFTGrid(seisCube[i]); //move new out of loop? Copy grid instead

    transf->setAccessMode(FFTGrid::RANDOMACCESS);
    transf->fftInPlace();
    transf->square();
    transf->invFFTInPlace();
    transf->collapseAndAdd( grid ); //the result of the collapse (the result for z=0) is is added to grid
    transf->endAccess();
    delete transf;
  }
  float sill = grid[0];
  for(int i=0;i<n;i++)
    (*corrXY)(i) = grid[i]/sill;
  delete [] grid;
}
