/***************************************************************************
*      Copyright (C) 2008 by Norwegian Computing Center and Statoil        *
***************************************************************************/


#ifndef BLOCKEDLOGSCOMMON_H
#define BLOCKEDLOGSCOMMON_H



class BlockedLogsCommon
{

public:
  BlockedLogsCommon(const NRLib::Well                * const well_data,
                    const std::vector<std::string>   & coordinate_logs_to_be_blocked,
                    const std::vector<std::string>   & cont_logs_to_be_blocked,
                    const std::vector<std::string>   & disc_logs_to_be_blocked,
                    const Simbox                     * const estimation_simbox,
                    bool                               interpolate, 
                    bool                             & failed,
                    std::string                      & err_text);

  ~ BlockedLogsCommon(void);


  //GET FUNCTIONS --------------------------------

  int                                    GetNumberOfBlocks()   const   { return n_blocks_                                                     ;}
  const std::vector<double>            & GetXpos(void)         const   { return coordinate_logs_[coordinate_log_names_.find("X_pos")->second] ;}
  const std::vector<double>            & GetYpos(void)         const   { return coordinate_logs_[coordinate_log_names_.find("Y_pos")->second] ;}
  const std::vector<double>            & GetZpos(void)         const   { return coordinate_logs_[coordinate_log_names_.find("TVD")->second]   ;}

  const std::vector<double>            & GetTVD(void)          const   { return continuous_logs_[continuous_log_names_.find("TVD")->second]   ;}
  const std::vector<double>            & GetTWT(void)          const   { return continuous_logs_[continuous_log_names_.find("TWT")->second]   ;}
  bool                                   HasContLog(std::string s)     { return continuous_log_names_.find(s) != continuous_log_names_.end()  ;}
  bool                                   HasDiscLog(std::string s)     { return discrete_log_names_.find(s) != discrete_log_names_.end()      ;}

private:

  // FUNCTIONS------------------------------------

  void    RemoveMissingLogValues(const NRLib::Well                  * const well_data,
                                 std::map<std::string, int>         & coordinate_log_names,
                                 std::map<std::string, int>         & continuous_log_names,
                                 std::map<std::string, int>         & discrete_log_names,
                                 std::vector<std::vector<double> >  & coordinate_logs,
                                 std::vector<std::vector<double> >  & continuous_logs,
                                 std::vector<std::vector<int> >     & discrete_logs,
                                 const std::vector<std::string>     & coordinate_logs_to_be_blocked,
                                 const std::vector<std::string>     & cont_logs_to_be_blocked,
                                 const std::vector<std::string>     & disc_logs_to_be_blocked,
                                 unsigned int                       & n_data,
                                 bool                               & failed);

  void    BlockWell(const NRLib::Well                         * const well_data,
                    const Simbox                              * const estimation_simbox,
                    const std::map<std::string, int>          & coordinate_log_names,
                    const std::map<std::string, int>          & continuous_log_names,
                    const std::map<std::string, int>          & discrete_log_names,
                    const std::vector<std::vector<double> >   & coordinate_logs,
                    const std::vector<std::vector<double> >   & continuous_logs,
                    const std::vector<std::vector<int> >      & discrete_logs,
                    std::map<std::string, int>                & coordinate_log_names_blocked,
                    std::map<std::string, int>                & continuous_log_names_blocked,
                    std::map<std::string, int>                & discrete_log_names_blocked,
                    std::vector<std::vector<double> >         & coordinate_logs_blocked,
                    std::vector<std::vector<double> >         & continuous_logs_blocked,
                    std::vector<std::vector<int> >            & discrete_logs_blocked,
                    unsigned int                                n_data,
                    bool                                        interpolate,
                    bool                                      & failed);

  void    FindSizeAndBlockPointers(const Simbox         * const estimation_simbox,
                                   std::vector<int>     & b_ind);

  void    FindBlockIJK(const Simbox                     * const estimation_simbox,
                       const std::vector<int>           & b_ind);

  void    BlockCoordinateLog(const std::vector<int>     &  b_ind,
                             const std::vector<double>  &  coord,
                             std::vector<double>        &  blocked_coord);

  void    BlockContinuousLog(const std::vector<int>     &  b_ind,
                             const std::vector<double>  &  well_log,
                             std::vector<double>        &  blocked_log);

  void    InterpolateContinuousLog(std::vector<double>   & blocked_log,
                                   int                     start,
                                   int                     end,
                                   int                     index,
                                   float                   rel);

  // CLASS VARIABLES -----------------------------

  std::string        well_name_;        ///< Name of well
  unsigned int       n_data_;           // Number of legal data values

  int                n_blocks_;         // number of blocks in well log


  std::vector<double> x_pos_;  // Simbox x position
  std::vector<double> y_pos_;  // Simbox y position
  std::vector<double> z_pos_;  // Simbox z position

  //std::vector<float> twt_;    // Two-way travel time (ms)
  //std::vector<float> tvd_;    // True vertical depth

  std::vector<int> i_pos_;    // Simbox i position
  std::vector<int> j_pos_;    // Simbox j position
  std::vector<int> k_pos_;    // Simbox k position

  std::map<std::string, int> coordinate_log_names_; // Map between coordinate name and vector position
  std::map<std::string, int> continuous_log_names_; // Map between continuous log name and vector position
  std::map<std::string, int> discrete_log_names_;   // Map between discrete log name and vector position

  std::map<std::string, int> coordinate_log_names_blocked_; //
  std::map<std::string, int> continuous_log_names_blocked_; //
  std::map<std::string, int> discrete_log_names_blocked_;   //

  int n_continuous_logs_;
  int n_discrete_logs_;

  std::vector<std::vector<double> > coordinate_logs_; //
  std::vector<std::vector<double> > continuous_logs_; //
  std::vector<std::vector<int> >    discrete_logs_;   //

  std::vector<std::vector<double> > coordinate_logs_blocked_;
  std::vector<std::vector<double> > continuous_logs_blocked_;
  std::vector<std::vector<int> >    discrete_logs_blocked_;

  int                       n_layers_;                 ///< Number of layers in estimation_simbox

  float                     dz_;                       ///< Simbox dz value for block

  int                       first_M_;                   ///< First well log entry contributing to blocked well
  int                       last_M_;                    ///< Last well log entry contributing to blocked well


  int                       first_B_;                   ///< First block with contribution from well log
  int                       last_B_;                    ///< Last block with contribution from well log

};

#endif