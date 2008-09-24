#ifndef MODEL_H
#define MODEL_H

#include <stdio.h>

#include "nrlib/surface/regularsurface.hpp"

#include "lib/global_def.h"
#include "src/definitions.h"
#include "src/background.h" //or move getAlpha & co to cpp-file.
#include "src/modelsettings.h"

struct irapgrid;
class Corr;
class ModelFile;
class Wavelet;
class Vario;
class Simbox;
class WellData;
class FFTGrid;
class RandomGen;
class GridMapping;

class Model
{
public:
  Model(char * fileName);
  ~Model();

  ModelSettings  * getModelSettings()         const { return modelSettings_          ;}
  Simbox         * getTimeSimbox()            const { return timeSimbox_             ;}
  //Simbox         * getDepthSimbox()           const { return depthSimbox_            ;}
  //Simbox         * getTimeCutSimbox()         const { return timeCutSimbox_          ;}
  WellData      ** getWells()                 const { return wells_                  ;}
  FFTGrid        * getBackAlpha()             const { return background_->getAlpha() ;}
  FFTGrid        * getBackBeta()              const { return background_->getBeta()  ;}
  FFTGrid        * getBackRho()               const { return background_->getRho()   ;}
  Corr           * getPriorCorrelations()     const { return priorCorrelations_      ;}
  float          * getPriorFacies()           const { return priorFacies_            ;}  
  FFTGrid       ** getSeisCubes()             const { return seisCube_               ;}
  Wavelet       ** getWavelets()              const { return wavelet_                ;}
  float         ** getAMatrix()               const { return reflectionMatrix_       ;}
  RandomGen      * getRandomGen()             const { return randomGen_              ;} 
  bool             hasSignalToNoiseRatio()    const { return hasSignalToNoiseRatio_  ;}
  GridMapping    * getTimeDepthMapping()      const { return timeDepthMapping_;}
  GridMapping    * getTimeCutMapping()        const { return timeCutMapping_;}
  bool             getVelocityFromInversion() const { return velocityFromInversion_  ;}
  bool             getFailed()                const { return failed_                 ;}
  void             releaseGrids();                                        // Cuts connection to SeisCube_ and  backModel_
  void             getCorrGradIJ(float & corrGradI, float &corrGradJ) const;

  enum             backFileTypes{STORMFILE = -2, SEGYFILE = -1};

private:
  void             makeTimeSimbox(Simbox        *& timeSimbox,
                                  ModelSettings *& modelSettings, 
                                  ModelFile      * modelFile,
                                  char           * errText,
                                  bool           & failed,
                                  Simbox         * &timeCutSimbox);
  void             setupExtendedTimeSimbox(Simbox * timeSimbox, 
                                           Surface * corrSurf, Simbox *& timeCutSimbox);
  void             completeTimeCutSimbox(Simbox       *& timeCutSimbox,
                                         ModelSettings * modelSettings,
                                         char            * errText,
                                         bool            &failed);
  void             processSeismic(FFTGrid      **& seisCube,
                                  Simbox        *& timeSimbox,
                                  ModelSettings *& modelSettings, 
                                  ModelFile      * modelFile,
                                  char           * errText,
                                  bool           & failed);
  void             processWells(WellData     **& wells,
                                Simbox         * timeSimbox,
                                RandomGen      * randomGen,
                                ModelSettings *& modelSettings, 
                                ModelFile      * modelFile,
                                char           * errText,
                                bool           & failed);
  void             processBackground(Background   *& background, 
                                     WellData     ** wells,
                                     Simbox        * timeSimbox,
                                     ModelSettings * modelSettings, 
                                     ModelFile     * modelFile, 
                                     char          * errText,
                                     bool          & failed);
  void             processPriorCorrelations(Corr         *& priorCorrelations,
                                            Background    * background,
                                            WellData     ** wells,
                                            Simbox        * timeSimbox,
                                            ModelSettings * modelSettings, 
                                            ModelFile     * modelFile,
                                            char          * errText,
                                            bool          & failed);
  void             processReflectionMatrix(float       **& reflectionMatrix,
                                           Background    * background,
                                           ModelSettings * modelSettings, 
                                           ModelFile     * modelFile,                  
                                           char          * errText,
                                           bool          & failed);
  void             processWavelets(Wavelet     **& wavelet,
                                   FFTGrid      ** seisCube,
                                   WellData     ** wells,
                                   float        ** reflectionMatrix,
                                   Simbox        * timeSimbox,
                                   Surface      ** shiftGrids,
                                   Surface      ** gainGrids,
                                   ModelSettings * modelSettings, 
                                   ModelFile     * modelFile,
                                   bool          & hasSignalToNoiseRatio,
                                   char          * errText,
                                   bool          & failed);
  void             processPriorFaciesProb(float        *& priorFacies,
                                          WellData     ** wells,
                                          RandomGen     * randomGen,
                                          int             nz,
                                          ModelSettings * modelSettings);
  void             processVelocity(FFTGrid      *& velocity,
                                   Simbox        * timeSimbox,
                                   ModelSettings * modelSettings, 
                                   ModelFile     * modelFile, 
                                   char          * errText,
                                   bool          & failed);
  void             setSimboxSurfaces(Simbox    *& simbox, 
                                     char      ** surfFile, 
                                     bool         parallelSurfaces, 
                                     double       dTop, 
                                     double       lz, 
                                     double       dz, 
                                     int          nz, 
                                     int        & error);
  void             estimateXYPaddingSizes(Simbox         * timeSimbox,
                                          ModelSettings *& modelSettings);
  void             estimateZPaddingSize(Simbox         * timeSimbox,
                                        ModelSettings *& modelSettings);
  int              readSegyFiles(char          ** fNames, 
                                 int              nFiles, 
                                 FFTGrid       ** target, 
                                 Simbox        *& timeSimbox, 
                                 ModelSettings *& modelSettings,
                                 char           * errText,
                                 int              fileno = -1); 
 
  int              readStormFile(char           * fName, 
                                 FFTGrid       *& target, 
                                 const char     * parName,
                                 Simbox         * timeSimbox,
                                 ModelSettings *& modelSettings, 
                                 char           * errText);
  void             estimateCorrXYFromSeismic(Surface *& CorrXY,
                                             FFTGrid ** seisCube,
                                             int        nAngles);
  Surface        * findCorrXYGrid();
  int              setPaddingSize(int   nx, 
                                  float px);
  void             loadExtraSurfaces(Surface  **& waveletEstimInterval,
                                     Surface  **& faciesEstimInterval,
                                     Simbox     * timeSimbox,
                                     ModelFile  * modelFile,
                                     char       * errText,
                                     bool       & failed);
  float         ** readMatrix(char       * fileName, 
                              int          n1, 
                              int          n2, 
                              const char * readReason, 
                              char       * errText);
  void             setupDefaultReflectionMatrix(float       **& reflectionMatrix,
                                                Background    * background,
                                                ModelSettings * modelSettings,
                                                ModelFile     * modelFile);
  int              findFileType(char * fileName);
  void             checkAvailableMemory(Simbox        * timeSimbox,
                                        ModelSettings * modelSettings);
  void             checkFaciesNames(WellData      ** wells,
                                    ModelSettings *& modelSettings,
                                    char           * tmpErrText,
                                    int            & error);
  void             printSettings(ModelSettings * modelSettings,
                                 ModelFile     * modelFile,
                                 bool            hasSignalToNoiseRatio);
  int              getWaveletFileFormat(char * fileName, 
                                        char * errText);
  //Compute correlation gradient in terms of i,j and k in grid.
  double *         findPlane(Surface * surf); //Finds plane l2-closest to surface.             
  //Create planar surface with same extent as template, p[0]+p[1]*x+p[2]*y
  Surface *        createPlaneSurface(double * planeParams, Surface * templateSurf);


  ModelSettings  * modelSettings_;
  Simbox         * timeSimbox_;            ///< Information about simulation area.
  
  WellData      ** wells_;                 ///< Well data
  Background     * background_;            ///< Holds the background model.
  Corr           * priorCorrelations_;     ///<
  FFTGrid       ** seisCube_;              ///< Seismic data cubes
  Wavelet       ** wavelet_;               ///< Wavelet for angle
  Surface       ** shiftGrids_;            ///< Grids containing shift data for wavelets
  Surface       ** gainGrids_;             ///< Grids containing gain data for wavelets.
  Surface       ** waveletEstimInterval_;  ///< Grids giving the wavelet estimation interval.
  Surface       ** faciesEstimInterval_;   ///< Grids giving the facies estimation intervals.
  Surface        * correlationDirection_;  ///< Grid giving the correlation direction.
  RandomGen      * randomGen_;             ///< Random generator.
  float          * priorFacies_;
  float         ** reflectionMatrix_;      ///< May specify own Zoeppritz-approximation. Default NULL,
                                           ///< indicating that standard approximation will be used.

  double           gradX_;                 ///< X-gradient of correlation rotation. 
  double           gradY_;                 ///< Y-gradient of correlation rotation.
                                           ///< These are only used with correaltion surfaces.

  bool             failed_;                ///< Indicates whether errors ocuured during construction. 
  GridMapping    * timeDepthMapping_;      ///< Contains both simbox nad maping used for depth conversion
  GridMapping    * timeCutMapping_;        ///< Simbox and mapping for timeCut

  bool             velocityFromInversion_;

  bool             hasSignalToNoiseRatio_; ///< Use SN ratio instead of error variance in model file. 
};

#endif

