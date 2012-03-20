#ifndef DISTRIBUTIONSROCKT0DEM_H
#define DISTRIBUTIONSROCKT0DEM_H

#include "rplib/rockdem.h"
#include "rplib/distributionsrockt0.h"

// Parallel classes are RockDEM and DistributionsGeochemicalDEM.
class DistributionsRockT0DEM : public DistributionsRockT0 {
public:

  DistributionsRockT0DEM() : DistributionsRockT0(){}

  virtual ~DistributionsRockT0DEM(){}

  virtual Rock  * GenerateSample(const std::vector<double> & /*trend_params*/) const {
    double par_dem = 1.1;              //FAKE
    Rock * rock = new RockDEM(par_dem);
    return rock;
  }

  virtual std::vector<double>   GetExpectation(const std::vector<double> & /*trend_params*/) const {
    std::vector<double> expectation(3, 0.0); //FAKE
    return expectation;
  }

  virtual NRLib::Grid2D<double> GetCovariance(const std::vector<double> & /*trend_params*/) const {
    NRLib::Grid2D<double> grid2d(3,3,5.0); //FAKE
    return grid2d;
  }

  virtual Pdf3D * GeneratePdf(void) const {
    Pdf3D * pdf3D = NULL; //FAKE
    return pdf3D;
  }

};

#endif
