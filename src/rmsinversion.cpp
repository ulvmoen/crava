/***************************************************************************
*      Copyright (C) 2008 by Norwegian Computing Center and Statoil        *
***************************************************************************/

#include "src/rmsinversion.h"
#include "src/modeltraveltimedynamic.h"
#include "src/seismicparametersholder.h"
#include "src/simbox.h"
#include "src/modelgeneral.h"
#include "src/rmstrace.h"
#include "src/kriging2d.h"
#include "src/krigingdata2d.h"
#include "src/covgrid2d.h"
#include "src/definitions.h"

#include "nrlib/flens/nrlib_flens.hpp"

#include "lib/timekit.hpp"


RMSInversion::RMSInversion(const ModelGeneral      * modelGeneral,
                           ModelTravelTimeDynamic  * modelTravelTimeDynamic,
                           SeismicParametersHolder & seismicParameters)
{

  LogKit::WriteHeader("Building Stochastic RMS Inversion Model");

  time_t time_start;
  time_t time_end;
  time(&time_start);

  double wall = 0.0;
  double cpu  = 0.0;
  TimeKit::getTime(wall,cpu);

  FFTGrid * mu_log_vp                      = seismicParameters.GetMuAlpha();
  FFTGrid * cov_log_vp                     = seismicParameters.GetCovBeta();
  std::vector<double> cov_grid_log_vp      = getCovLogVp(cov_log_vp);

  Simbox * timeSimbox                      = modelGeneral->getTimeSimbox();
  const Simbox * simbox_above              = modelTravelTimeDynamic->getSimboxAbove();
  const Simbox * simbox_below              = modelTravelTimeDynamic->getSimboxBelow();
  const std::vector<RMSTrace *> rms_traces = modelTravelTimeDynamic->getRMSTraces();
  const double mu_vp_top                   = modelTravelTimeDynamic->getMeanVpTop();
  const double mu_vp_base                  = modelTravelTimeDynamic->getMeanVpBase();
  const double var_vp_above                = modelTravelTimeDynamic->getVarVpAbove();
  const double var_vp_below                = modelTravelTimeDynamic->getVarVpBelow();
  const double range_above                 = modelTravelTimeDynamic->getRangeAbove();
  const double range_below                 = modelTravelTimeDynamic->getRangeBelow();
  const double standard_deviation          = modelTravelTimeDynamic->getStandardDeviation();

  Vario * variogram_above                  = new GenExpVario(1, static_cast<float>(range_above));
  Vario * variogram_below                  = new GenExpVario(1, static_cast<float>(range_below));

  n_above_                                 = simbox_above->getnz();
  n_below_                                 = simbox_below->getnz();
  n_model_                                 = timeSimbox  ->getnz();
  n_pad_above_                             = n_above_ + static_cast<int>(std::ceil(range_above));
  n_pad_below_                             = n_below_ + static_cast<int>(std::ceil(range_below));
  n_pad_model_                             = mu_log_vp->getNzp();

  int nxp                                  = mu_log_vp->getNxp();
  int nyp                                  = mu_log_vp->getNyp();

  int n_rms_traces                         = static_cast<int>(rms_traces.size());

  float corrGradI;
  float corrGradJ;
  modelGeneral->getCorrGradIJ(corrGradI, corrGradJ);

  double dt_max_above = simbox_above->getdz();
  double dt_max_below = simbox_below->getdz();

  NRLib::Grid2D<double> Sigma_vp_above = generateSigmaVp(dt_max_above, n_pad_above_, var_vp_above, variogram_above);
  NRLib::Grid2D<double> Sigma_vp_below = generateSigmaVp(dt_max_below, n_pad_below_, var_vp_below, variogram_below);

  std::vector<double> cov_circulant_above(n_pad_above_, 0);
  std::vector<double> cov_circulant_below(n_pad_below_, 0);
  std::vector<double> cov_circulant_model(n_pad_model_, 0);

  std::vector<KrigingData2D> mu_log_vp_post_above(n_pad_above_);
  std::vector<KrigingData2D> mu_log_vp_post_model(n_pad_model_);
  std::vector<KrigingData2D> mu_log_vp_post_below(n_pad_below_);

  for (int i = 0; i < n_rms_traces; i++) {

    std::vector<double>   mu_post;
    NRLib::Grid2D<double> Sigma_post;

    do1DInversion(mu_vp_top,
                  mu_vp_base,
                  Sigma_vp_above,
                  Sigma_vp_below,
                  standard_deviation,
                  rms_traces[i],
                  mu_log_vp,
                  cov_grid_log_vp,
                  simbox_above,
                  simbox_below,
                  timeSimbox,
                  mu_post,
                  Sigma_post);

    setExpectation(rms_traces[i],
                   mu_post,
                   mu_log_vp_post_above,
                   mu_log_vp_post_model,
                   mu_log_vp_post_below);

    addCovariance(n_rms_traces,
                  Sigma_post,
                  cov_circulant_above,
                  cov_circulant_model,
                  cov_circulant_below);

  }

  LogKit::LogFormatted(LogKit::Low, "\nIn reservoir zone:");

  FFTGrid * post_mu_log_vp_model = NULL;

  krigeExpectation3D(timeSimbox, mu_log_vp_post_model, nxp, nyp, post_mu_log_vp_model);

  FFTGrid * stationary_d          = NULL;
  FFTGrid * stationary_covariance = NULL;

  std::vector<int> observation_filter;

  generateStationaryDistribution(cov_grid_log_vp,
                                 cov_circulant_model,
                                 n_rms_traces,
                                 modelGeneral->getPriorCorrXY(),
                                 corrGradI,
                                 corrGradJ,
                                 mu_log_vp,
                                 post_mu_log_vp_model,
                                 stationary_d,
                                 stationary_covariance,
                                 observation_filter);
  // From second time lapse
  FFTGrid * post_log_vp = NULL;

  calculateLogVpExpectation(observation_filter,
                            seismicParameters.getPriorVar0(),
                            mu_log_vp,
                            cov_log_vp,
                            stationary_d,
                            stationary_covariance,
                            post_log_vp);

  NRLib::Grid<double> distance;

  calculateDistanceGrid(timeSimbox,
                        mu_log_vp,
                        post_log_vp,
                        distance);

  Simbox * new_simbox = NULL;
  std::string errTxt  = "";

  generateNewSimbox(distance,
                    modelTravelTimeDynamic->getLzLimit(),
                    timeSimbox,
                    new_simbox,
                    errTxt);

  NRLib::Grid<double> resample_grid;

  generateResampleGrid(distance, timeSimbox, new_simbox, resample_grid);

  delete post_log_vp;
  delete new_simbox;
  // To here

  calculateFullPosteriorModel(observation_filter,
                              seismicParameters,
                              stationary_d,
                              stationary_covariance);

  time(&time_end);
  LogKit::LogFormatted(LogKit::DebugLow,"\nTime elapsed :  %d\n",time_end-time_start);

  delete variogram_above;
  delete variogram_below;

  delete post_mu_log_vp_model;

  delete stationary_d;
  delete stationary_covariance;

}

//-----------------------------------------------------------------------------------------//

RMSInversion::~RMSInversion()
{
}

//-----------------------------------------------------------------------------------------//
void
RMSInversion::do1DInversion(const double                & mu_vp_top,
                            const double                & mu_vp_base,
                            const NRLib::Grid2D<double> & Sigma_m_above,
                            const NRLib::Grid2D<double> & Sigma_m_below,
                            const double                & standard_deviation,
                            const RMSTrace              * rms_trace,
                            const FFTGrid               * mu_log_vp,
                            const std::vector<double>   & cov_grid_log_vp,
                            const Simbox                * simbox_above,
                            const Simbox                * simbox_below,
                            const Simbox                * timeSimbox,
                            std::vector<double>         & mu_post_log_vp,
                            NRLib::Grid2D<double>       & Sigma_post_log_vp) const
{

  int i_ind = rms_trace->getIIndex();
  int j_ind = rms_trace->getJIndex();

  double t_top     = timeSimbox->getTop(i_ind, j_ind);
  double t_bot     = timeSimbox->getBot(i_ind, j_ind);

  double dt_above  = simbox_above->getdz(i_ind, j_ind);
  double dt_below  = simbox_below->getdz(i_ind, j_ind);
  double dt_simbox = timeSimbox  ->getdz(i_ind,  j_ind);

  const std::vector<double> time = rms_trace->getTime();

  NRLib::Grid2D<double> G = calculateG(time,
                                       t_top,
                                       t_bot,
                                       dt_above,
                                       dt_below,
                                       dt_simbox);

  std::vector<double>   mu_log_vp_model = generateMuLogVpModel(mu_log_vp, i_ind, j_ind);

  std::vector<double>   mu_m_square;
  NRLib::Grid2D<double> Sigma_m_square;

  calculateMuSigma_mSquare(mu_log_vp_model,
                           cov_grid_log_vp,
                           mu_vp_top,
                           mu_vp_base,
                           Sigma_m_above,
                           Sigma_m_below,
                           mu_m_square,
                           Sigma_m_square);

  std::vector<double>   d_square       = calculateDSquare(rms_trace->getVelocity());
  NRLib::Grid2D<double> Sigma_d_square = calculateSigmaDSquare(rms_trace->getVelocity(), standard_deviation);

  std::vector<double>   mu_post;
  NRLib::Grid2D<double> Sigma_post;

  calculatePosteriorModel(d_square,
                          Sigma_d_square,
                          mu_m_square,
                          Sigma_m_square,
                          G,
                          mu_post,
                          Sigma_post);

  transformVpSquareToLogVp(mu_post,
                           Sigma_post,
                           mu_post_log_vp,
                           Sigma_post_log_vp);

}
//-----------------------------------------------------------------------------------------//
void
RMSInversion::calculatePosteriorModel(const std::vector<double>   & d,
                                      const NRLib::Grid2D<double> & Sigma_d,
                                      const std::vector<double>   & mu_m,
                                      const NRLib::Grid2D<double> & Sigma_m,
                                      const NRLib::Grid2D<double> & G,
                                      std::vector<double>         & mu_post,
                                      NRLib::Grid2D<double>       & Sigma_post) const
{
  int n_layers = static_cast<int>(mu_m.size());
  int n_data   = static_cast<int>(d.size());

  NRLib::Vector mu_m1(n_layers);
  for (int i = 0; i < n_layers; i++)
    mu_m1(i) = mu_m[i];

  NRLib::Matrix Sigma_m1(n_layers, n_layers);
  for (int i = 0; i < n_layers; i++) {
    for (int j = 0; j < n_layers; j++)
      Sigma_m1(i,j) = Sigma_m(i,j);
  }

  NRLib::Matrix G1(n_data, n_layers);
  for (int i = 0; i < n_data; i++) {
    for (int j = 0; j < n_layers; j++)
      G1(i,j) = G(i,j);
  }

  NRLib::Matrix G1_transpose(n_layers, n_data);
  for (int i = 0; i < n_data; i++) {
    for (int j = 0; j < n_layers; j++)
      G1_transpose(j,i) = G(i,j);
  }

  NRLib::Vector d1(n_data);
  for (int i = 0; i < n_data; i++)
    d1(i) = d[i];

  NRLib::Matrix Sigma_d1(n_data, n_data);
  for (int i = 0; i < n_data; i++) {
    for (int j = 0; j < n_data; j++)
      Sigma_d1(i,j) = Sigma_d(i,j);
  }

  NRLib::Vector dataMean            = G1 * mu_m1;
  NRLib::Vector diff                = d1 - dataMean;
  NRLib::Matrix dataModelCovariance = G1 * Sigma_m1;
  NRLib::Matrix modelDataCovariance = Sigma_m1 * G1_transpose;
  NRLib::Matrix dataCovariance      = dataModelCovariance * G1_transpose + Sigma_d1;

  NRLib::SymmetricMatrix data_covariance_inv_sym(n_data);

  for (int i = 0; i < n_data; i++) {
    for (int j = 0; j <= i; j++)
      data_covariance_inv_sym(j,i) = dataCovariance(i,j);
  }

  NRLib::CholeskyInvert(data_covariance_inv_sym);

  NRLib::Matrix data_covariance_inv(n_data, n_data);
  for (int i = 0; i < n_data; i++) {
    for (int j = i; j < n_data; j++) {
      data_covariance_inv(i,j) = data_covariance_inv_sym(i,j);
      data_covariance_inv(j,i) = data_covariance_inv(i,j);
    }
  }

  NRLib::Matrix helpMat     = modelDataCovariance * data_covariance_inv;
  NRLib::Vector mu_help     = helpMat * diff;
  NRLib::Matrix Sigma_help  = helpMat * dataModelCovariance;

  NRLib::Vector mu_post1    = mu_m1 + mu_help;
  NRLib::Matrix Sigma_post1 = Sigma_m1 - Sigma_help;

  mu_post.resize(n_layers);
  for (int i = 0; i < n_layers; i++)
    mu_post[i] = mu_post1(i);

  Sigma_post.Resize(n_layers, n_layers);
  for (int i = 0; i < n_layers; i++) {
    for (int j = 0; j < n_layers; j++)
      Sigma_post(i,j) = Sigma_post1(i,j);
  }

}

//-----------------------------------------------------------------------------------------//
std::vector<double>
RMSInversion::calculateDSquare(const std::vector<double> & d) const
{
  int n = static_cast<int>(d.size());

  std::vector<double> d_square(n);

  for (int i = 0; i < n; i++)
    d_square[i] = std::pow(d[i],2);

  return d_square;
}
//-----------------------------------------------------------------------------------------//

NRLib::Grid2D<double>
RMSInversion::calculateG(const std::vector<double> & rms_time,
                         const double              & t_top,
                         const double              & t_bot,
                         const double              & dt_above,
                         const double              & dt_below,
                         const double              & dt_simbox) const
{
  int n_layers = n_pad_above_ + n_pad_model_ + n_pad_below_;

  std::vector<double> t(n_layers + 1, 0);
  std::vector<double> dt(n_layers + 1, 0);

  for (int j = 0; j < n_layers + 1; j++) {
    if(j < n_above_) {
      t[j]  = j * dt_above;
      dt[j] = dt_above;
    }
    else if(j >= n_pad_above_ && j < n_pad_above_ + n_model_) {
      t[j]  = t_top + (j - n_pad_above_) * dt_simbox;
      dt[j] = dt_simbox;
    }
    else if(j >= n_pad_above_ + n_pad_model_ && j < n_pad_above_ + n_pad_model_ + n_below_) {
      t[j]  = t_bot + (j - n_pad_above_ - n_pad_model_) * dt_below;
      dt[j] = dt_below;
    }
  }

  int n_rms_data = static_cast<int>(rms_time.size());

  NRLib::Grid2D<double> G(n_rms_data, n_layers, 0);

  for (int j = 0; j < n_rms_data; j++) {
    int k=0;
    double prev_t = t[0];
    while (rms_time[j] >= t[k] && k < n_layers) {
      G(j,k) = dt[k] / rms_time[j];
      if (t[k] != 0)
        prev_t = t[k];
      k++;
    }
    if (k < n_layers) {
      if (t[k-1] == 0)
        G(j,k) = (rms_time[j] - prev_t) / rms_time[j];
      else
        G(j,k) = (rms_time[j] - t[k-1]) / rms_time[j];
    }
  }

  return G;
}

//-----------------------------------------------------------------------------------------//

NRLib::Grid2D<double>
RMSInversion::calculateSigmaDSquare(const std::vector<double> & rms_velocity,
                                    const double              & standard_deviation) const
{
  int n                 = static_cast<int>(rms_velocity.size());
  const double variance = std::pow(standard_deviation,2);

  NRLib::Grid2D<double> I(n, n, 0);
  for (int i = 0; i < n; i++)
    I(i, i) = 1;

  NRLib::Grid2D<double> Sigma_d_square(n, n, 0);

  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++)
      Sigma_d_square(i, j) = 4 * std::pow(rms_velocity[i], 2) * variance * I(i, j) + 2 * std::pow(variance, 2) * I(i, j);
  }

  return Sigma_d_square;
}

//-----------------------------------------------------------------------------------------//
void
RMSInversion::calculateMuSigma_mSquare(const std::vector<double>   & mu_log_vp_model,
                                       const std::vector<double>   & cov_grid_log_vp,
                                       const double                & mu_vp_top,
                                       const double                & mu_vp_base,
                                       const NRLib::Grid2D<double> & Sigma_vp_above,
                                       const NRLib::Grid2D<double> & Sigma_vp_below,
                                       std::vector<double>         & mu_vp_square,
                                       NRLib::Grid2D<double>       & Sigma_vp_square) const
{

  int n_layers_simbox = static_cast<int>(mu_log_vp_model.size());

  // Model
  NRLib::Grid2D<double> Sigma_log_vp_model = generateSigmaModel(cov_grid_log_vp);

  // Above
  std::vector<double>   mu_vp_above    = generateMuVpAbove(mu_vp_top, std::exp(mu_log_vp_model[0]));
  std::vector<double>   mu_log_vp_above;
  NRLib::Grid2D<double> Sigma_log_vp_above;
  calculateCentralMomentLogNormalInverse(mu_vp_above, Sigma_vp_above, mu_log_vp_above, Sigma_log_vp_above);

  // Below
  std::vector<double>   mu_vp_below    = generateMuVpBelow(std::exp(mu_log_vp_model[n_layers_simbox-1]), mu_vp_base);
  std::vector<double>   mu_log_vp_below;
  NRLib::Grid2D<double> Sigma_log_vp_below;
  calculateCentralMomentLogNormalInverse(mu_vp_below, Sigma_vp_below, mu_log_vp_below, Sigma_log_vp_below);

  // Combine
  std::vector<double>   mu_log_m    = generateMuCombined(mu_log_vp_above, mu_log_vp_model, mu_log_vp_below);
  NRLib::Grid2D<double> Sigma_log_m = generateSigmaCombined(Sigma_log_vp_above, Sigma_log_vp_model, Sigma_log_vp_below);

  // Transform to Vp^2
  calculateSecondCentralMomentLogNormal(mu_log_m, Sigma_log_m, mu_vp_square, Sigma_vp_square);

}
//-----------------------------------------------------------------------------------------//

NRLib::Grid2D<double>
RMSInversion::generateSigmaVp(const double & dt,
                              const int    & n_layers,
                              const double & var_vp,
                              const Vario  * variogram) const
{

  // Circulant corrT
  std::vector<double> corrT(n_layers, 0);
  corrT[0] = 1;

  for (int i = 1; i < n_layers / 2; i++) {
    corrT[i] = static_cast<double>(variogram->corr(static_cast<float>(i * dt), 0));
    corrT[n_layers - i] = corrT[i];
  }

  NRLib::Grid2D<double> Sigma_vp = generateSigma(var_vp, corrT);

  return Sigma_vp;
}

//-----------------------------------------------------------------------------------------//
std::vector<double>
RMSInversion::getCovLogVp(const FFTGrid * cov_log_vp) const
{
  int n_layers_padding = cov_log_vp->getNzp();

  std::vector<double> cov_grid_log_vp(n_layers_padding, 0);
  for (int j = 0; j < n_layers_padding; j++)
    cov_grid_log_vp[j] = cov_log_vp->getRealValue(0, 0, j, true);

  return cov_grid_log_vp;

}

//-----------------------------------------------------------------------------------------//

std::vector<double>
RMSInversion::generateMuLogVpModel(const FFTGrid * mu_log_vp,
                                   const int     & i_ind,
                                   const int     & j_ind) const
{

  int n_layers_padding = mu_log_vp->getNzp();

  std::vector<double> mu_grid_log_vp(n_layers_padding, 0);
  for (int j = 0; j < n_layers_padding; j++)
    mu_grid_log_vp[j] = mu_log_vp->getRealValue(i_ind, j_ind, j, true);

  return mu_grid_log_vp;

}

//-----------------------------------------------------------------------------------------//

std::vector<double>
RMSInversion::generateMuVpAbove(const double & top_value,
                                const double & base_value) const
{
  std::vector<double> mu_vp = generateMuVp(top_value, base_value, n_above_);

  mu_vp.resize(n_pad_above_);

  for (int i = n_above_; i < n_pad_above_; i++)
    mu_vp[i] = mu_vp[n_above_ - 1];

  return mu_vp;
}

//-----------------------------------------------------------------------------------------//

std::vector<double>
RMSInversion::generateMuVpBelow(const double & top_value,
                                const double & base_value) const
{
  std::vector<double> mu_vp = generateMuVp(top_value, base_value, n_below_);

  for (int i = 0; i < n_below_; i++)
    mu_vp[i] = mu_vp[i + 1];

  int diff = n_pad_below_ - n_below_;

  std::vector<double> mu_vp_out(n_pad_below_, mu_vp[0]);

  for (int i = diff; i < n_pad_below_; i++)
    mu_vp_out[i] = mu_vp[i-diff];


  return mu_vp_out;
}

//-----------------------------------------------------------------------------------------//

std::vector<double>
RMSInversion::generateMuVp(const double & top_value,
                           const double & base_value,
                           const int    & n_layers) const
{
  std::vector<double> mu_vp(n_layers+1);

  for (int j = 0; j < n_layers + 1; j++)
    mu_vp[j] = top_value + j * (base_value - top_value) / n_layers;

  return mu_vp;
}

//-----------------------------------------------------------------------------------------//

std::vector<double>
RMSInversion::generateMuCombined(const std::vector<double> & mu_above,
                                 const std::vector<double> & mu_model,
                                 const std::vector<double> & mu_below) const
{
  int n_layers_above        = static_cast<int>(mu_above.size());
  int n_layers_below        = static_cast<int>(mu_below.size());
  int n_layers_model        = static_cast<int>(mu_model.size());
  int n_layers              = n_layers_above + n_layers_model + n_layers_below;

  std::vector<double> mu_m(n_layers, RMISSING);

  for (int j = 0; j < n_layers_above; j++)
    mu_m[j] = mu_above[j];

  for (int j = n_layers_above; j < n_layers_above + n_layers_model; j++)
    mu_m[j] = mu_model[j - n_layers_above];

  for (int j = n_layers_above+n_layers_model; j < n_layers; j++)
    mu_m[j] = mu_below[j - n_layers_above - n_layers_model];

  return mu_m;
}

//-----------------------------------------------------------------------------------------//

NRLib::Grid2D<double>
RMSInversion::generateSigmaCombined(const NRLib::Grid2D<double> & Sigma_above,
                                    const NRLib::Grid2D<double> & Sigma_model,
                                    const NRLib::Grid2D<double> & Sigma_below) const
{
  int n_layers_above = static_cast<int>(Sigma_above.GetNI());
  int n_layers_below = static_cast<int>(Sigma_below.GetNI());
  int n_layers_model = static_cast<int>(Sigma_model.GetNI());
  int n_layers       = n_layers_above + n_layers_model + n_layers_below;

  NRLib::Grid2D<double> Sigma_m(n_layers, n_layers, 0);

  for (int j = 0; j < n_layers_above; j++) {
    for (int k = j; k < n_layers_above; k++) {
      Sigma_m(j, k) = Sigma_above(j, k);
      Sigma_m(k, j) = Sigma_m(j, k);
    }
  }

  for (int j = n_layers_above; j < n_layers_above+n_layers_model; j++) {
    for (int k = j; k < n_layers_above+n_layers_model; k++) {
      Sigma_m(j, k) = Sigma_model(j - n_layers_above, k - n_layers_above);
      Sigma_m(k, j) = Sigma_m(j, k);
    }
  }

  for (int j = n_layers_above + n_layers_model; j < n_layers; j++) {
    for (int k = j; k < n_layers; k++) {
      Sigma_m(j, k) = Sigma_below(j - n_layers_above - n_layers_model, k - n_layers_above - n_layers_model);
      Sigma_m(k, j) = Sigma_m(j, k);
    }
  }

  return Sigma_m;
}

//-----------------------------------------------------------------------------------------//

NRLib::Grid2D<double>
RMSInversion::generateSigma(const double              & var,
                            const std::vector<double> & corrT) const
{

  int n_layers = static_cast<int>(corrT.size());

  NRLib::Grid2D<double> Sigma(n_layers, n_layers, 0);

  for (int j = 0; j < n_layers; j++) {
    int count = 0;
    for (int k = j; k < n_layers; k++) {
      Sigma(j, k) = var * corrT[count];
      Sigma(k, j) = Sigma(j, k);
      count ++;
    }
  }

  return Sigma;
}

//-----------------------------------------------------------------------------------------//
NRLib::Grid2D<double>
RMSInversion::generateSigmaModel(const std::vector<double> & cov_grid) const
{
  int n = static_cast<int>(cov_grid.size());

  NRLib::Grid2D<double> Sigma_m(n, n);

  for (int j = 0; j < n; j++) {
    int count = 0;
    for (int k = j; k < n; k++) {
      double cov_log_vp = cov_grid[count];
      Sigma_m(j, k) = cov_log_vp;
      Sigma_m(k, j) = Sigma_m(j, k);
      count++;
    }
  }

  return Sigma_m;
}
//-----------------------------------------------------------------------------------------//

void
RMSInversion::transformVpToVpSquare(const std::vector<double>   & mu_vp,
                                    const NRLib::Grid2D<double> & Sigma_vp,
                                    std::vector<double>         & mu_vp_square,
                                    NRLib::Grid2D<double>       & Sigma_vp_square) const
{
  std::vector<double>   mu_log_vp;
  NRLib::Grid2D<double> Sigma_log_vp;

  calculateCentralMomentLogNormalInverse(mu_vp, Sigma_vp, mu_log_vp, Sigma_log_vp);

  calculateSecondCentralMomentLogNormal(mu_log_vp, Sigma_log_vp, mu_vp_square, Sigma_vp_square);
}

//-----------------------------------------------------------------------------------------//

void
RMSInversion::transformVpSquareToVp(const std::vector<double>   & mu_vp_square,
                                    const NRLib::Grid2D<double> & Sigma_vp_square,
                                    std::vector<double>         & mu_vp,
                                    NRLib::Grid2D<double>       & Sigma_vp) const
{
  std::vector<double>   mu_log_vp;
  NRLib::Grid2D<double> Sigma_log_vp;

  calculateCentralMomentLogNormalInverse(mu_vp_square, Sigma_vp_square, mu_log_vp, Sigma_log_vp);

  calculateHalfCentralMomentLogNormal(mu_log_vp, Sigma_log_vp, mu_vp, Sigma_vp);
}

//-----------------------------------------------------------------------------------------//

void
RMSInversion::transformVpSquareToLogVp(const std::vector<double>   & mu_vp_square,
                                       const NRLib::Grid2D<double> & Sigma_vp_square,
                                       std::vector<double>         & mu_log_vp,
                                       NRLib::Grid2D<double>       & Sigma_log_vp) const
{
  int n_layers = static_cast<int>(mu_vp_square.size());

  calculateCentralMomentLogNormalInverse(mu_vp_square, Sigma_vp_square, mu_log_vp, Sigma_log_vp);

  for (int i = 0; i < n_layers; i++)
    mu_log_vp[i] = mu_log_vp[i] / 2;

  for (int i = 0; i < n_layers; i++) {
    for (int j = 0; j < n_layers; j++)
      Sigma_log_vp(i, j) = Sigma_log_vp(i, j) / 4;
  }
}

//-----------------------------------------------------------------------------------------//

void
RMSInversion::calculateCentralMomentLogNormal(const std::vector<double>   & mu_log_vp,
                                              const NRLib::Grid2D<double> & variance_log_vp,
                                              std::vector<double>         & mu_vp_trans,
                                              NRLib::Grid2D<double>       & variance_vp_trans) const
{
  int n = static_cast<int>(mu_log_vp.size());

  mu_vp_trans.resize(n);
  for (int i = 0; i < n; i++)
    mu_vp_trans[i] = std::exp(mu_log_vp[i] + 0.5 * variance_log_vp(i, i));

  variance_vp_trans.Resize(n, n);
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++)
      variance_vp_trans(i, j) = mu_vp_trans[i] * mu_vp_trans[j] * (std::exp(variance_log_vp(i, j)) - 1);
  }

}

//-----------------------------------------------------------------------------------------//

void
RMSInversion::calculateCentralMomentLogNormalInverse(const std::vector<double>   & mu_vp_trans,
                                                     const NRLib::Grid2D<double> & variance_vp_trans,
                                                     std::vector<double>         & mu_log_vp,
                                                     NRLib::Grid2D<double>       & variance_log_vp) const
{
  int n = static_cast<int>(mu_vp_trans.size());

  variance_log_vp.Resize(n, n);
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++)
      variance_log_vp(i, j) = std::log(1 + variance_vp_trans(i, j) / (mu_vp_trans[i] * mu_vp_trans[j]));
  }

  mu_log_vp.resize(n);
  for (int i = 0; i < n; i++)
    mu_log_vp[i] = std::log(mu_vp_trans[i]) - 0.5 * variance_log_vp(i, i);

}

//-----------------------------------------------------------------------------------------//
void
RMSInversion::calculateSecondCentralMomentLogNormal(const std::vector<double>   & mu_log_vp,
                                                    const NRLib::Grid2D<double> & variance_log_vp,
                                                    std::vector<double>         & mu_vp_square,
                                                    NRLib::Grid2D<double>       & variance_vp_square) const
{
  int n_layers = static_cast<int>(mu_log_vp.size());

  std::vector<double> mu(n_layers);
  for (int i = 0; i < n_layers; i++)
    mu[i] = mu_log_vp[i] * 2;

  NRLib::Grid2D<double> variance(n_layers, n_layers);
  for (int i = 0; i < n_layers; i++) {
    for (int j = 0; j < n_layers; j++)
      variance(i, j) = variance_log_vp(i, j) * 4;
  }

  mu_vp_square.resize(n_layers);
  variance_vp_square.Resize(n_layers, n_layers);

  calculateCentralMomentLogNormal(mu, variance, mu_vp_square, variance_vp_square);
}

//-----------------------------------------------------------------------------------------//

void
RMSInversion::calculateHalfCentralMomentLogNormal(const std::vector<double>   & mu_log_vp,
                                                  const NRLib::Grid2D<double> & variance_log_vp,
                                                  std::vector<double>         & mu_vp,
                                                  NRLib::Grid2D<double>       & variance_vp) const
{
  int n_layers = static_cast<int>(mu_log_vp.size());

  std::vector<double> mu(n_layers);
  for (int i = 0; i < n_layers; i++)
    mu[i] = mu_log_vp[i] / 2;

  NRLib::Grid2D<double> variance(n_layers, n_layers);
  for (int i = 0; i < n_layers; i++) {
    for (int j = 0; j <n_layers; j++)
      variance(i, j) = variance_log_vp(i, j) / 4;
  }

  mu_vp.resize(n_layers);
  variance_vp.Resize(n_layers, n_layers);

  calculateCentralMomentLogNormal(mu, variance, mu_vp, variance_vp);
}
//-----------------------------------------------------------------------------------------//

void
RMSInversion::addCovariance(const int                   & n_rms_traces,
                            const NRLib::Grid2D<double> & Sigma_post,
                            std::vector<double>         & cov_stationary_above,
                            std::vector<double>         & cov_stationary_model,
                            std::vector<double>         & cov_stationary_below) const
{
  NRLib::Grid2D<double> cov_above(n_pad_above_, n_pad_above_);
  for (int i = 0; i < n_pad_above_; i++) {
    for (int j = 0; j < n_pad_above_; j++)
      cov_above(i, j) = Sigma_post(i, j);
  }

  NRLib::Grid2D<double> cov_model(n_pad_model_, n_pad_model_);
  for (int i = 0; i < n_pad_model_; i++) {
    for (int j = 0; j < n_pad_model_; j++)
      cov_model(i, j) = Sigma_post(i + n_pad_above_, j + n_pad_above_);
  }

  NRLib::Grid2D<double> cov_below(n_pad_below_, n_pad_below_);
  for (int i = 0; i < n_pad_below_; i++) {
    for (int j = 0; j < n_pad_below_; j++)
      cov_below(i, j) = Sigma_post(i + n_pad_above_ + n_pad_model_, j + n_pad_above_+n_pad_model_);
  }

  std::vector<double> circulant_above = makeCirculantCovariance(cov_above, n_above_);
  std::vector<double> circulant_model = makeCirculantCovariance(cov_model, n_model_);
  std::vector<double> circulant_below = makeCirculantCovariance(cov_below, n_below_);

  for (int i = 0; i < n_pad_above_; i++)
    cov_stationary_above[i] += circulant_above[i] / n_rms_traces;

  for (int i = 0; i < n_pad_model_; i++)
    cov_stationary_model[i] += circulant_model[i] / n_rms_traces;

  for (int i = 0; i < n_pad_below_; i++)
    cov_stationary_below[i] += circulant_below[i] / n_rms_traces;
}

//-----------------------------------------------------------------------------------------//

std::vector<double>
RMSInversion::makeCirculantCovariance(const NRLib::Grid2D<double> & cov,
                                      const int                   & n_nopad) const
{
  int n = static_cast<int>(cov.GetNI());

  NRLib::Grid2D<int> I(n, n, 0);
  for (int i = 0; i < n_nopad; i++) {
    for (int j = i; j < n; j++)
      I(i, j) = 1;
  }

  std::vector<double> covT(n, 0);
  std::vector<int>    count(n, 0);

  for (int i = 0; i < n; i++) {
    int place = 0;
    for (int j = i; j < n; j++) {
      covT[place]  += cov(i, j) * I(i, j);
      count[place] += I(i, j);
      place++;
    }
  }

  for (int i = 0; i < n; i++)
    covT[i] /= count[i];

  for (int i = 1; i <n / 2; i++) {
    covT[i] = (covT[i] + covT[n - i]) / 2;
    covT[n - i] = covT[i];
  }

  return covT;
}

//-------------------------------------------------------------------------------

void
RMSInversion::setExpectation(const RMSTrace             * rms_trace,
                             const std::vector<double>  & post_vp,
                             std::vector<KrigingData2D> & mu_log_vp_post_above,
                             std::vector<KrigingData2D> & mu_log_vp_post_model,
                             std::vector<KrigingData2D> & mu_log_vp_post_below) const
{

  int i_ind = rms_trace->getIIndex();
  int j_ind = rms_trace->getJIndex();

  for (int i = 0; i < n_pad_above_; i++)
    mu_log_vp_post_above[i].addData(i_ind, j_ind, static_cast<float>(post_vp[i]));

  for (int i = 0; i < n_pad_model_; i++)
    mu_log_vp_post_model[i].addData(i_ind, j_ind, static_cast<float>(post_vp[i + n_pad_above_]));

  for (int i = 0; i < n_pad_below_; i++)
    mu_log_vp_post_below[i].addData(i_ind, j_ind, static_cast<float>(post_vp[i + n_pad_above_ + n_pad_model_]));

}

//-------------------------------------------------------------------------------

void
RMSInversion::krigeExpectation3D(const Simbox                * simbox,
                                 std::vector<KrigingData2D>  & kriging_post,
                                 const int                   & nxp,
                                 const int                   & nyp,
                                 FFTGrid                    *& mu_post) const
{

  std::string text = "\nKriging 1D posterior RMS velocities to 3D:";

  LogKit::LogFormatted(LogKit::Low, text);

  const int    nx   = simbox->getnx();
  const int    ny   = simbox->getny();
  const int    nz   = simbox->getnz();

  const int    rnxp = 2 * (nx / 2 + 1);

  const double x0   = simbox->getx0();
  const double y0   = simbox->gety0();
  const double lx   = simbox->getlx();
  const double ly   = simbox->getly();


  FFTGrid * bgGrid = ModelGeneral::createFFTGrid(nx, ny, nz, nx, ny, nz, false); // Grid without padding
  bgGrid->createRealGrid();
  bgGrid->setType(FFTGrid::PARAMETER);

  Vario * lateralVario = new GenExpVario(1, 2000, 2000);
  CovGrid2D covGrid2D = Kriging2D::makeCovGrid2D(simbox, lateralVario, 0);

  //
  // Template surface to be kriged
  //
  Surface surface(x0, y0, lx, ly, nx, ny, RMISSING);

  float monitorSize = std::max(1.0f, static_cast<float>(nz) * 0.02f);
  float nextMonitor = monitorSize;
  std::cout
    << "\n  0%       20%       40%       60%       80%      100%"
    << "\n  |    |    |    |    |    |    |    |    |    |    |  "
    << "\n  ^";

  bgGrid->setAccessMode(FFTGrid::WRITE);

  for (int k = 0 ; k < nz ; k++) {

    kriging_post[k].findMeanValues();

    int n_data = kriging_post[k].getNumberOfData();

    double mean_value = 0;
    for (int i = 0; i < n_data; i++)
      mean_value += static_cast<double>(kriging_post[k].getData(i) / n_data);

    surface.Assign(mean_value);

    // Kriging of layer
    Kriging2D::krigSurface(surface, kriging_post[k], covGrid2D);

    // Set layer in background model from surface
    for (int j = 0; j < ny; j++) {
      for (int i = 0; i < rnxp ; i++) {
        if (i < nx)
          bgGrid->setNextReal(float(surface(i, j)));
        else
          bgGrid->setNextReal(0);
      }
    }

    // Log progress
    if (k + 1 >= static_cast<int>(nextMonitor)) {
      nextMonitor += monitorSize;
      std::cout << "^";
      fflush(stdout);
    }
  }

  bgGrid->endAccess();


  const int nzp  = static_cast<int>(kriging_post.size());

  mu_post = ModelGeneral::createFFTGrid(nx, ny, nz, nxp, nyp, nzp, false);
  mu_post->createRealGrid();
  mu_post->setType(FFTGrid::PARAMETER);

  Background::createPaddedParameter(mu_post, bgGrid);

  delete bgGrid;

  delete lateralVario;
}

//-------------------------------------------------------------------------------

void
RMSInversion::generateStationaryDistribution(const std::vector<double> & pri_circulant_cov,
                                             const std::vector<double> & post_circulant_cov,
                                             const int                 & n_rms_traces,
                                             const Surface             * priorCorrXY,
                                             const float               & corrGradI,
                                             const float               & corrGradJ,
                                             FFTGrid                   * pri_mu,
                                             FFTGrid                   * post_mu,
                                             FFTGrid                  *& stationary_observations,
                                             FFTGrid                  *& stationary_covariance,
                                             std::vector<int>          & observation_filter) const
{
  int nx   = pri_mu->getNx();
  int ny   = pri_mu->getNy();
  int nz   = pri_mu->getNz();
  int nxp  = pri_mu->getNxp();
  int nyp  = pri_mu->getNyp();
  int nzp  = pri_mu->getNzp();
  int cnzp = nzp/2 + 1;
  int rnzp = 2 * cnzp;

  fftw_real    * pri_cov_r = new fftw_real[rnzp];
  fftw_complex * pri_cov_c = reinterpret_cast<fftw_complex*>(pri_cov_r);

  for (int i = 0; i < nzp; i++)
    pri_cov_r[i] = static_cast<float>(pri_circulant_cov[i]);

  Utils::fft(pri_cov_r, pri_cov_c, nzp);

  for (int i = 0; i < cnzp; i++)
    pri_cov_c[i].im = 0;

  fftw_real    * post_cov_r = new fftw_real[rnzp];
  fftw_complex * post_cov_c = reinterpret_cast<fftw_complex*>(post_cov_r);

  for (int i = 0; i < nzp; i++)
    post_cov_r[i] = static_cast<float>(post_circulant_cov[i]);

  Utils::fft(post_cov_r, post_cov_c, nzp);

  for (int i = 0; i < cnzp; i++)
    post_cov_c[i].im = 0;

  fftw_real    * var_e_r = new fftw_real[rnzp];
  fftw_complex * var_e_c = reinterpret_cast<fftw_complex*>(var_e_r);

  calculateErrorVariance(pri_cov_c, post_cov_c, nzp, var_e_c);

  stationary_observations = ModelGeneral::createFFTGrid(nx, ny, nz, nxp, nyp, nzp, false);
  stationary_observations->createRealGrid();
  stationary_observations->setType(FFTGrid::DATA);

  calculateStationaryObservations(pri_cov_c,
                                  var_e_c,
                                  pri_mu,
                                  post_mu,
                                  stationary_observations);

  calculateFilter(pri_cov_c, post_cov_c, nzp, observation_filter);

  float factor = static_cast<float>(nx * ny) / static_cast<float>(n_rms_traces); // Multiply var_e_c with this factor to increase the variance as the RMS data are only observed in some of the traces

  Utils::fftInv(var_e_c, var_e_r, nzp);
  for (int i = 0; i < nzp; i++)
    var_e_r[i] *= factor;

  stationary_covariance = ModelGeneral::createFFTGrid(nx, ny, nz, nxp, nyp, nzp, false);
  stationary_covariance->createRealGrid();
  stationary_covariance->setType(FFTGrid::COVARIANCE);

  stationary_covariance->fillInParamCorr(priorCorrXY, var_e_r, corrGradI, corrGradJ);

  delete [] pri_cov_r;
  delete [] post_cov_r;
  delete [] var_e_r;
}

//-------------------------------------------------------------------------------

void
RMSInversion::calculateStationaryObservations(const fftw_complex  * pri_cov_c,
                                              const fftw_complex  * var_e_c,
                                              FFTGrid             * pri_mu,
                                              FFTGrid             * post_mu,
                                              FFTGrid            *& stat_d) const
{
  // Calculate d = mu_pri + (var_pri+var_e)/conj_var_pri * (mu_post-mu_pri)

  int nyp = pri_mu->getNyp();
  int nzp = pri_mu->getNzp();
  int cnxp = pri_mu->getCNxp();
  int cnzp = nzp/2 + 1;

  fftw_complex * add_c        = new fftw_complex[cnzp];
  fftw_complex * conj_pri_cov = new fftw_complex[cnzp];
  fftw_complex * divide_c     = new fftw_complex[cnzp];

  addComplex(pri_cov_c, var_e_c, cnzp, add_c);
  complexConjugate(pri_cov_c, cnzp, conj_pri_cov);
  divideComplex(add_c, conj_pri_cov, cnzp, divide_c);

  delete [] conj_pri_cov;
  delete [] add_c;

  pri_mu ->fftInPlace();
  post_mu->fftInPlace();
  stat_d ->fftInPlace();

  pri_mu ->setAccessMode(FFTGrid::READ);
  post_mu->setAccessMode(FFTGrid::READ);
  stat_d ->setAccessMode(FFTGrid::WRITE);

  fftw_complex div;
  fftw_complex subtract;
  fftw_complex multiply;
  fftw_complex d;

  for (int k = 0; k < nzp; k++) {
    if (k < cnzp)
      div = divide_c[k];
    else {
      div.re =  divide_c[nzp-k].re;
      div.im = -divide_c[nzp-k].im;
    }
    for (int j = 0; j < nyp; j++) {
      for (int i = 0; i < cnxp; i++) {
        fftw_complex pri_c  = pri_mu ->getNextComplex();
        fftw_complex post_c = post_mu->getNextComplex();

        subtractComplex(&post_c, &pri_c, 1, &subtract);
        multiplyComplex(&div, &subtract, 1, &multiply);
        addComplex(&pri_c, &multiply, 1, &d);

        stat_d->setNextComplex(d);
      }
    }
  }

  pri_mu ->endAccess();
  post_mu->endAccess();
  stat_d ->endAccess();

  pri_mu ->invFFTInPlace();
  post_mu->invFFTInPlace();
  stat_d ->invFFTInPlace();

  delete [] divide_c;

}

//-------------------------------------------------------------------------------

void
RMSInversion::calculateErrorVariance(const fftw_complex * pri_cov_c,
                                     const fftw_complex * post_cov_c,
                                     const int          & nzp,
                                     fftw_complex       * var_e_c) const
{
  // Calculate var_e = var_pri *(conj_var_pri - var_pri + var_post) / (var_pri-var_post)

  int cnzp = nzp/2 + 1;

  fftw_complex * conj_pri_cov  = new fftw_complex[cnzp];
  fftw_complex * diff_prior    = new fftw_complex[cnzp];
  fftw_complex * nominator_sum = new fftw_complex[cnzp];
  fftw_complex * mult_c        = new fftw_complex[cnzp];
  fftw_complex * subtract_c    = new fftw_complex[cnzp];

  complexConjugate(pri_cov_c, cnzp, conj_pri_cov);
  subtractComplex(conj_pri_cov, pri_cov_c, cnzp, diff_prior);
  addComplex(diff_prior, post_cov_c, cnzp, nominator_sum);
  multiplyComplex(pri_cov_c, nominator_sum, cnzp, mult_c);
  subtractComplex(pri_cov_c, post_cov_c, cnzp, subtract_c);
  divideComplex(mult_c, subtract_c, cnzp, var_e_c);

  delete [] conj_pri_cov;
  delete [] diff_prior;
  delete [] nominator_sum;
  delete [] mult_c;
  delete [] subtract_c;
}

//-------------------------------------------------------------------------------

void
RMSInversion::calculateFilter(const fftw_complex * pri_cov_c,
                              const fftw_complex * post_cov_c,
                              const int          & nzp,
                              std::vector<int>   & filter) const
{
  // Fudge factor 1.04:
  // Rules out cases where the error standard deviation is
  // 5 times larger than the standard deviation in the parameter, 5 = sqrt(1/0.04)

  int cnzp = nzp/2 + 1;

  std::vector<double> abs_pri;
  std::vector<double> abs_post;

  absoulteComplex(pri_cov_c, cnzp, abs_pri);
  absoulteComplex(post_cov_c, cnzp, abs_post);

  filter.resize(nzp, 0);

  for (int i = 0; i < cnzp; i++) {
    if (abs_pri[i] > abs_post[i] * 1.04)
      filter[i] = 1;
  }

  for (int i = cnzp; i < nzp; i++)
    filter[i] = filter[nzp-i];

}

//-----------------------------------------------------------------------------------------//

void
RMSInversion::multiplyComplex(const fftw_complex * z1,
                              const fftw_complex * z2,
                              const int          & n,
                              fftw_complex       * z) const
{
  for (int i = 0; i < n; i++) {
    z[i].re = z1[i].re * z2[i].re - z1[i].im * z2[i].im;
    z[i].im = z1[i].re * z2[i].im + z1[i].im * z2[i].re;
  }
}

//-----------------------------------------------------------------------------------------//

void
RMSInversion::divideComplex(const fftw_complex * z1,
                            const fftw_complex * z2,
                            const int          & n,
                            fftw_complex       * z) const
{
  for (int i = 0; i < n; i++) {
    z[i].re = (z1[i].re * z2[i].re + z1[i].im * z2[i].im) / (std::pow(z2[i].re, 2) + std::pow(z2[i].im, 2));
    z[i].im = (z2[i].re * z1[i].im - z1[i].re * z2[i].im) / (std::pow(z2[i].re, 2) + std::pow(z2[i].im, 2));
  }
}

//-----------------------------------------------------------------------------------------//

void
RMSInversion::addComplex(const fftw_complex * z1,
                         const fftw_complex * z2,
                         const int          & n,
                         fftw_complex       * z) const
{
  for (int i = 0; i < n; i++) {
    z[i].re = z1[i].re + z2[i].re;
    z[i].im = z1[i].im + z2[i].im;
  }
}

//-----------------------------------------------------------------------------------------//

void
RMSInversion::subtractComplex(const fftw_complex * z1,
                              const fftw_complex * z2,
                              const int          & n,
                              fftw_complex       * z) const
{
  for (int i = 0; i < n; i++) {
    z[i].re = z1[i].re - z2[i].re;
    z[i].im = z1[i].im - z2[i].im;
  }
}

//-----------------------------------------------------------------------------------------//

void
RMSInversion::absoulteComplex(const fftw_complex  * z,
                              const int           & n,
                              std::vector<double> & abs_z) const
{
  abs_z.resize(n);

  for (int i = 0; i < n; i++)
    abs_z[i] = std::sqrt(std::pow(z[i].re, 2) + std::pow(z[i].im, 2));
}

//-----------------------------------------------------------------------------------------//

void
RMSInversion::complexConjugate(const fftw_complex  * z,
                               const int           & n,
                               fftw_complex        * conj_z) const
{

  for (int i = 0; i < n; i++) {
    conj_z[i].re =   z[i].re;
    conj_z[i].im = - z[i].im;
  }
}

//-----------------------------------------------------------------------------------------//

void
RMSInversion::calculateFullPosteriorModel(const std::vector<int>  & observation_filter,
                                          SeismicParametersHolder & seismic_parameters,
                                          FFTGrid                 * stationary_observations,
                                          FFTGrid                 * stationary_observation_covariance) const
{
  seismic_parameters.FFTAllGrids();

  FFTGrid * mu_vp  = seismic_parameters.GetMuAlpha();
  FFTGrid * mu_vs  = seismic_parameters.GetMuBeta();
  FFTGrid * mu_rho = seismic_parameters.GetMuRho();

  FFTGrid * cov_vp  = seismic_parameters.GetCovAlpha();
  FFTGrid * cov_vs  = seismic_parameters.GetCovBeta();
  FFTGrid * cov_rho = seismic_parameters.GetCovRho();

  FFTGrid * cr_cov_vp_vs  = seismic_parameters.GetCrCovAlphaBeta();
  FFTGrid * cr_cov_vp_rho = seismic_parameters.GetCrCovAlphaRho();
  FFTGrid * cr_cov_vs_rho = seismic_parameters.GetCrCovBetaRho();

  mu_vp ->setAccessMode(FFTGrid::READANDWRITE);
  mu_vs ->setAccessMode(FFTGrid::READANDWRITE);
  mu_rho->setAccessMode(FFTGrid::READANDWRITE);

  cov_vp ->setAccessMode(FFTGrid::READANDWRITE);
  cov_vs ->setAccessMode(FFTGrid::READANDWRITE);
  cov_rho->setAccessMode(FFTGrid::READANDWRITE);

  cr_cov_vp_vs ->setAccessMode(FFTGrid::READANDWRITE);
  cr_cov_vp_rho->setAccessMode(FFTGrid::READANDWRITE);
  cr_cov_vs_rho->setAccessMode(FFTGrid::READANDWRITE);

  if (stationary_observations->getIsTransformed() == false)
    stationary_observations->fftInPlace();

  if (stationary_observation_covariance->getIsTransformed() == false)
    stationary_observation_covariance->fftInPlace();

  int cnxp = mu_vp->getCNxp();
  int nyp  = mu_vp->getNyp();
  int nzp  = mu_vp->getNzp();

  fftw_complex * mu_m    = new fftw_complex[3];
  fftw_complex * mu_post = new fftw_complex[3];

  fftw_complex data;
  fftw_complex data_variance;
  fftw_complex var_d;
  fftw_complex diff;
  fftw_complex divide;
  fftw_complex mu_help;
  fftw_complex multiply;
  fftw_complex Sigma_help;

  fftw_complex ** Sigma_m = new fftw_complex*[3];
  for (int i = 0; i < 3; i++)
    Sigma_m[i] = new fftw_complex[3];

  fftw_complex ** Sigma_post = new fftw_complex*[3];
  for (int i = 0; i < 3; i++)
    Sigma_post[i] = new fftw_complex[3];

  for (int k = 0; k < nzp; k++) {

    int filter = observation_filter[k];

    for (int j = 0; j < nyp; j++) {
      for (int i = 0; i < cnxp; i++) {

        data          = stationary_observations          ->getNextComplex();
        data_variance = stationary_observation_covariance->getNextComplex();

        mu_m[0] = mu_vp ->getNextComplex();
        mu_m[1] = mu_vs ->getNextComplex();
        mu_m[2] = mu_rho->getNextComplex();

        seismic_parameters.getNextParameterCovariance(Sigma_m);

        if (filter == 0) {
          // Prior = posterior
          mu_vp ->setNextComplex(mu_m[0]);
          mu_vs ->setNextComplex(mu_m[1]);
          mu_rho->setNextComplex(mu_m[2]);

          cov_vp ->setNextComplex(Sigma_m[0][0]);
          cov_vs ->setNextComplex(Sigma_m[1][1]);
          cov_rho->setNextComplex(Sigma_m[2][2]);

          cr_cov_vp_vs ->setNextComplex(Sigma_m[0][1]);
          cr_cov_vp_rho->setNextComplex(Sigma_m[0][2]);
          cr_cov_vs_rho->setNextComplex(Sigma_m[1][2]);
        }
        else {
          addComplex(&Sigma_m[0][0], &data_variance, 1, &var_d);
          subtractComplex(&data, &mu_m[0], 1, &diff);
          divideComplex(&diff, &var_d, 1, &divide);

          for (int ii = 0; ii < 3; ii++) {
            multiplyComplex(&Sigma_m[ii][0], &divide, 1, &mu_help);
            addComplex(&mu_m[ii], &mu_help, 1, &mu_post[ii]);
          }

          for (int ii = 0; ii < 3; ii++) {
            for (int jj = 0; jj < 3; jj++) {
              multiplyComplex(&Sigma_m[ii][0], &Sigma_m[0][jj], 1, &multiply);
              divideComplex(&multiply, &var_d, 1, &Sigma_help);
              subtractComplex(&Sigma_m[ii][jj], &Sigma_help, 1, &Sigma_post[ii][jj]);
            }
          }

          mu_vp ->setNextComplex(mu_post[0]);
          mu_vs ->setNextComplex(mu_post[1]);
          mu_rho->setNextComplex(mu_post[2]);

          cov_vp ->setNextComplex(Sigma_post[0][0]);
          cov_vs ->setNextComplex(Sigma_post[1][1]);
          cov_rho->setNextComplex(Sigma_post[2][2]);

          cr_cov_vp_vs ->setNextComplex(Sigma_post[0][1]);
          cr_cov_vp_rho->setNextComplex(Sigma_post[0][2]);
          cr_cov_vs_rho->setNextComplex(Sigma_post[1][2]);

        }
      }
    }
  }

  mu_vp ->endAccess();
  mu_vs ->endAccess();
  mu_rho->endAccess();

  cov_vp ->endAccess();
  cov_vs ->endAccess();
  cov_rho->endAccess();

  cr_cov_vp_vs ->endAccess();
  cr_cov_vp_rho->endAccess();
  cr_cov_vs_rho->endAccess();

  delete [] mu_m;
  delete [] mu_post;

  for(int i=0; i<3; i++)
    delete [] Sigma_m[i];

  for(int i=0; i<3; i++)
    delete [] Sigma_post[i];
}

//-----------------------------------------------------------------------------------------//

void
RMSInversion::calculateLogVpExpectation(const std::vector<int>  & observation_filter,
                                        const NRLib::Matrix     & prior_var_vp,
                                        FFTGrid                 * mu_vp,
                                        FFTGrid                 * cov_vp,
                                        FFTGrid                 * stationary_observations,
                                        FFTGrid                 * stationary_observation_covariance,
                                        FFTGrid                *& post_mu_vp) const
{
  if (mu_vp->getIsTransformed() == false)
    mu_vp->fftInPlace();
  if (cov_vp->getIsTransformed() == false)
    cov_vp->fftInPlace();

  mu_vp ->setAccessMode(FFTGrid::READ);
  cov_vp->setAccessMode(FFTGrid::READ);

  if (stationary_observations->getIsTransformed() == false)
    stationary_observations->fftInPlace();

  if (stationary_observation_covariance->getIsTransformed() == false)
    stationary_observation_covariance->fftInPlace();

  int nx   = mu_vp->getNx();
  int ny   = mu_vp->getNy();
  int nz   = mu_vp->getNz();
  int nxp  = mu_vp->getNxp();
  int cnxp = mu_vp->getCNxp();
  int nyp  = mu_vp->getNyp();
  int nzp  = mu_vp->getNzp();

  fftw_complex mu_m;
  fftw_complex var_m;
  fftw_complex Sigma_m;
  fftw_complex mu_post;

  fftw_complex data;
  fftw_complex data_variance;
  fftw_complex var_d;
  fftw_complex diff;
  fftw_complex divide;
  fftw_complex mu_help;

  post_mu_vp = ModelGeneral::createFFTGrid(nx, ny, nz, nxp, nyp, nzp, false);
  post_mu_vp->createComplexGrid();
  post_mu_vp->setType(FFTGrid::PARAMETER);
  post_mu_vp->setAccessMode(FFTGrid::WRITE);

  for (int k = 0; k < nzp; k++) {

    int filter = observation_filter[k];

    for (int j = 0; j < nyp; j++) {
      for (int i = 0; i < cnxp; i++) {

        data          = stationary_observations          ->getNextComplex();
        data_variance = stationary_observation_covariance->getNextComplex();
        mu_m          = mu_vp                            ->getNextComplex();
        var_m         = cov_vp                           ->getNextComplex();

        Sigma_m = SeismicParametersHolder::getParameterCovariance(prior_var_vp, 0, 0, var_m);

        if (filter == 0)
          post_mu_vp  ->setNextComplex(mu_m);
        else {
          addComplex(&Sigma_m, &data_variance, 1, &var_d);
          subtractComplex(&data, &mu_m, 1, &diff);
          divideComplex(&diff, &var_d, 1, &divide);

          multiplyComplex(&Sigma_m, &divide, 1, &mu_help);
          addComplex(&mu_m, &mu_help, 1, &mu_post);

          post_mu_vp  ->setNextComplex(mu_post);

        }
      }
    }
  }

  mu_vp     ->endAccess();
  cov_vp    ->endAccess();
  post_mu_vp->endAccess();

}

//-----------------------------------------------------------------------------------------//

void
RMSInversion::calculateDistanceGrid(const Simbox        * simbox,
                                    FFTGrid             * mu_vp,
                                    FFTGrid             * post_mu_vp,
                                    NRLib::Grid<double> & distance) const
{

  if (mu_vp->getIsTransformed() == true)
    mu_vp->invFFTInPlace();

  if (post_mu_vp->getIsTransformed() == true)
    post_mu_vp->invFFTInPlace();

  int nx  = simbox->getnx();
  int ny  = simbox->getny();
  int nz  = simbox->getnz();

  distance.Resize(nx, ny, nz);

  mu_vp     ->setAccessMode(FFTGrid::READ);
  post_mu_vp->setAccessMode(FFTGrid::READ);

  double d1; // time from top to base of a cell in the old time grid
  double d2; // time from top to base of a cell in the new time grid
  double v1; // old velocity, being prior velocity
  double v2; // new velocity, being posterior velocity

  for (int k = 0; k < nz; k++) {
    for (int j = 0; j < ny; j++) {
      for (int i = 0; i < nx; i++) {
        d1 = simbox    ->getdz(i, j);
        v1 = mu_vp     ->getRealValue(i, j, k);
        v2 = post_mu_vp->getRealValue(i, j, k);

        d2 = v1 * d1 / v2;

        distance(i, j, k) = d2;
      }
    }
  }

  mu_vp     ->endAccess();
  post_mu_vp->endAccess();
}

//-----------------------------------------------------------------------------------------//

void
RMSInversion::generateNewSimbox(const NRLib::Grid<double>  & distance,
                                const double               & lz_limit,
                                Simbox                     * simbox,
                                Simbox                    *& new_simbox,
                                std::string                & errTxt) const
{
  Surface base_surface;
  calculateBaseSurface(distance, simbox, base_surface);

  const int nz = simbox->getnz();

  Surface top_surface(dynamic_cast<const Surface &> (simbox->GetTopSurface()));

  new_simbox = new Simbox(simbox);
  new_simbox->setDepth(top_surface, base_surface, nz);
  new_simbox->calculateDz(lz_limit, errTxt);

}

//-----------------------------------------------------------------------------------------//

void
RMSInversion::generateResampleGrid(const NRLib::Grid<double> & distance,
                                   const Simbox              * old_simbox,
                                   const Simbox              * new_simbox,
                                   NRLib::Grid<double>       & resample_grid) const
{
  int nx  = new_simbox->getnx();
  int ny  = new_simbox->getny();
  int nz  = new_simbox->getnz();

  resample_grid.Resize(nx, ny, nz, 0);

  double tk; // k * dz
  double d1; // time from top to base of a cell in the old time grid
  double dz; // time from top to base of a cell in the new time grid
  double r;  // Position of tk between t2(l), t2(l+1)
  int    l;  // Largest integer satisfying t2(l) <= tk

  for (int i = 0; i < nx; i++) {
    for (int j = 0; j < ny; j++) {

      d1 = old_simbox->getdz(i, j);
      dz = new_simbox->getdz(i, j);

      std::vector<double> t2(nz + 1, 0);
      for (int k = 1; k < nz + 1; ++k)
        t2[k] = t2[k - 1] + distance(i, j, k - 1);

      l = 0;
      for (int k = 0; k < nz; k++) {
        tk = k * dz;

        while(tk >= t2[l])
          l++;
        l--;

        r = (tk - t2[l]) / (t2[l+1] - t2[l]);

        resample_grid(i, j, k) =  (l + r) * d1;
      }

    }
  }
}

//-----------------------------------------------------------------------------------------//

void
RMSInversion::calculateBaseSurface(const NRLib::Grid<double> & distance,
                                   const Simbox              * simbox,
                                   Surface                   & base_surface) const
{

  Surface top_surface(dynamic_cast<const Surface &> (simbox->GetTopSurface()));

  base_surface = top_surface;

  //
  // If a constant time surface has been used, it may have only four grid
  // nodes. To handle this situation we use the grid resolution whenever
  // this is larger than the surface resolution.
  //

  int ni = static_cast<int>(top_surface.GetNI());
  int nj = static_cast<int>(top_surface.GetNJ());

  int maxNx = std::max(simbox->getnx(), ni);
  int maxNy = std::max(simbox->getny(), nj);

  base_surface.Resize(maxNx, maxNy);

  double dx = 0.5 * top_surface.GetDX();
  double dy = 0.5 * top_surface.GetDY();

  int nz = simbox->getnz();

  for (int j = 0; j < nj; ++j) {
    for (int i = 0; i < ni; ++i) {

      double x, y;
      top_surface.GetXY(i, j, x, y);

      int ii, jj;
      simbox->getIndexes(x, y, ii, jj);

      if (ii != IMISSING && jj != IMISSING) {
        double sum = 0.0;

        for (int k = 0; k < nz; ++k)
          sum += distance(ii, jj, k);

        base_surface(i, j) = sum;
      }
      else {
        int i1, i2, i3, i4, j1, j2, j3, j4;
        simbox->getIndexes(x + dx, y + dy, i1, j1);
        simbox->getIndexes(x - dx, y - dy, i2, j2);
        simbox->getIndexes(x + dx, y - dy, i3, j3);
        simbox->getIndexes(x - dx, y + dy, i4, j4);

        int n = 0;
        if (i1 != IMISSING && j1 != IMISSING)
          n++;
        if (i2 != IMISSING && j2 != IMISSING)
          n++;
        if (i3 != IMISSING && j3 != IMISSING)
          n++;
        if (i4 != IMISSING && j4 != IMISSING)
          n++;

        if (n == 0)
          base_surface.SetMissing(i, j);

        else {
          double sum = 0.0;

          for (int k = 0; k < nz; ++k) {
            if (i1 != IMISSING && j1 != IMISSING)
              sum += distance(i1, j1, k);

            if (i2 != IMISSING && j2 != IMISSING)
              sum += distance(i2, j2, k);

            if (i3 != IMISSING && j3 != IMISSING)
              sum += distance(i3, j3, k);

            if (i4 != IMISSING && j4 != IMISSING)
              sum += distance(i4, j4, k);
          }

          base_surface(i, j) = sum / static_cast<double>(n);
        }
      }
    }
  }

  base_surface.AddNonConform(&top_surface);
}
