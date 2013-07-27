/*
 * Copyright (C) 2012-2013 Simon Lynen, ASL, ETH Zurich, Switzerland
 * You can contact the author at <slynen at ethz dot ch>
 * Copyright (C) 2011-2012 Stephan Weiss, ASL, ETH Zurich, Switzerland
 * You can contact the author at <stephan dot weiss at ieee dot org>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef POSE_MEASUREMENT_HPP_
#define POSE_MEASUREMENT_HPP_

#include <msf_core/msf_types.hpp>
#include <msf_core/msf_measurement.h>
#include <msf_core/msf_core.h>
#include <msf_updates/PoseDistorter.h>

namespace msf_updates{
namespace pose_measurement{
enum
{
  nMeasurements = 7
};
/**
 * \brief a measurement as provided by a pose tracking algorithm
 */
typedef msf_core::MSF_Measurement<geometry_msgs::PoseWithCovarianceStamped, Eigen::Matrix<double, nMeasurements, nMeasurements>, msf_updates::EKFState> PoseMeasurementBase;
struct PoseMeasurement : public PoseMeasurementBase
{
private:
  typedef PoseMeasurementBase Measurement_t;
  typedef Measurement_t::Measurement_ptr measptr_t;

  virtual void makeFromSensorReadingImpl(measptr_t msg)
  {

    Eigen::Matrix<double, nMeasurements, msf_core::MSF_Core<msf_updates::EKFState>::nErrorStatesAtCompileTime> H_old;
    Eigen::Matrix<double, nMeasurements, 1> r_old;

    H_old.setZero();
//    R_.setZero(); //already done in ctor of base

    // get measurements
    z_p_ = Eigen::Matrix<double, 3, 1>(msg->pose.pose.position.x, msg->pose.pose.position.y, msg->pose.pose.position.z);
    z_q_ = Eigen::Quaternion<double>(msg->pose.pose.orientation.w, msg->pose.pose.orientation.x,
                                     msg->pose.pose.orientation.y, msg->pose.pose.orientation.z);

    if(distorter_){
      static double tlast = 0;
      if(tlast != 0){
        double dt = time - tlast;
        distorter_->distort(z_p_, z_q_, dt);
      }
      tlast = time;
    }

    if (fixed_covariance_)//  take fix covariance from reconfigure GUI
    {

      const double s_zp = n_zp_ * n_zp_;
      const double s_zq = n_zq_ * n_zq_;
      R_ = (Eigen::Matrix<double, nMeasurements, 1>() << s_zp, s_zp, s_zp, s_zq, s_zq, s_zq, 1e-6).finished().asDiagonal();

    }else{// take covariance from sensor

      R_.block<6, 6>(0, 0) = Eigen::Matrix<double, 6, 6>(&msg->pose.covariance[0]);

      if(msg->header.seq % 100 == 0){ //only do this check from time to time
        if(R_.block<6, 6>(0, 0).determinant() < -0.001)
          MSF_WARN_STREAM_THROTTLE(60,"The covariance matrix you provided for the pose sensor is not positive definite: "<<(R_.block<6, 6>(0, 0)));
      }

      //clear cross-correlations between q and p
      R_.block<3, 3>(0, 3) = Eigen::Matrix<double, 3, 3>::Zero();
      R_.block<3, 3>(3, 0) = Eigen::Matrix<double, 3, 3>::Zero();
      R_(6, 6) = 1e-6; // q_wv yaw-measurement noise //TODO: WHY???

      /*************************************************************************************/
      // use this if your pose sensor is ethzasl_ptam (www.ros.org/wiki/ethzasl_ptam)
      // ethzasl_ptam publishes the camera pose as the world seen from the camera
      if (!measurement_world_sensor_)
      {
        Eigen::Matrix<double, 3, 3> C_zq = z_q_.toRotationMatrix();
        z_q_ = z_q_.conjugate();
        z_p_ = -C_zq.transpose() * z_p_;

        Eigen::Matrix<double, 6, 6> C_cov(Eigen::Matrix<double, 6, 6>::Zero());
        C_cov.block<3, 3>(0, 0) = C_zq;
        C_cov.block<3, 3>(3, 3) = C_zq;

        R_.block<6, 6>(0, 0) = C_cov.transpose() * R_.block<6, 6>(0, 0) * C_cov;
      }
      /*************************************************************************************/
    }
  }
public:
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW

  Eigen::Quaternion<double> z_q_; /// attitude measurement camera seen from world
  Eigen::Matrix<double, 3, 1> z_p_; /// position measurement camera seen from world
  double n_zp_, n_zq_; /// position and attitude measurement noise

  bool measurement_world_sensor_;
  bool fixed_covariance_;
  msf_updates::PoseDistorter::Ptr distorter_;
  int fixedstates_;

  typedef msf_updates::EKFState EKFState_T;
  typedef EKFState_T::StateSequence_T StateSequence_T;
  typedef EKFState_T::StateDefinition_T StateDefinition_T;
  virtual ~PoseMeasurement()
  {
  }
  PoseMeasurement(double n_zp, double n_zq, bool measurement_world_sensor, bool fixed_covariance, bool isabsoluteMeasurement, int sensorID, int fixedstates, msf_updates::PoseDistorter::Ptr distorter = msf_updates::PoseDistorter::Ptr()) :
    PoseMeasurementBase(isabsoluteMeasurement, sensorID),
    n_zp_(n_zp), n_zq_(n_zq), measurement_world_sensor_(measurement_world_sensor), fixed_covariance_(fixed_covariance), distorter_(distorter), fixedstates_(fixedstates)
  {
  }
  virtual std::string type(){
    return "pose";
  }

  virtual void calculateH(shared_ptr<EKFState_T> state_in, Eigen::Matrix<double, nMeasurements, msf_core::MSF_Core<EKFState_T>::nErrorStatesAtCompileTime>& H){
    const EKFState_T& state = *state_in; //get a const ref, so we can read core states

    H.setZero();

    // get rotation matrices
    Eigen::Matrix<double, 3, 3> C_wv = state.get<StateDefinition_T::q_wv>().toRotationMatrix();
    Eigen::Matrix<double, 3, 3> C_q = state.get<StateDefinition_T::q>().toRotationMatrix();
    Eigen::Matrix<double, 3, 3> C_ci = state.get<StateDefinition_T::q_ic>().conjugate().toRotationMatrix();

    // preprocess for elements in H matrix
    Eigen::Matrix<double, 3, 1> vecold;
    vecold = (state.get<StateDefinition_T::p>() + C_q * state.get<StateDefinition_T::p_ic>()) * state.get<StateDefinition_T::L>();
    Eigen::Matrix<double, 3, 3> skewold = skew(vecold);

    Eigen::Matrix<double, 3, 3> pci_sk = skew(state.get<StateDefinition_T::p_ic>());

    //get indices of states in error vector
    enum{
      idxstartcorr_p_ = msf_tmp::getStartIndexInCorrection<StateSequence_T, StateDefinition_T::p>::value,
      idxstartcorr_v_ = msf_tmp::getStartIndexInCorrection<StateSequence_T, StateDefinition_T::v>::value,
      idxstartcorr_q_ = msf_tmp::getStartIndexInCorrection<StateSequence_T, StateDefinition_T::q>::value,
      idxstartcorr_L_ = msf_tmp::getStartIndexInCorrection<StateSequence_T, StateDefinition_T::L>::value,
      idxstartcorr_qwv_ = msf_tmp::getStartIndexInCorrection<StateSequence_T, StateDefinition_T::q_wv>::value,
      idxstartcorr_pwv_ = msf_tmp::getStartIndexInCorrection<StateSequence_T, StateDefinition_T::p_wv>::value,
      idxstartcorr_qic_ = msf_tmp::getStartIndexInCorrection<StateSequence_T, StateDefinition_T::q_ic>::value,
      idxstartcorr_pic_ = msf_tmp::getStartIndexInCorrection<StateSequence_T, StateDefinition_T::p_ic>::value,
    };

    //read the fixed states flags
    bool scalefix = (fixedstates_ & 1 << StateDefinition_T::L);
    bool calibposfix = (fixedstates_ & 1 << StateDefinition_T::p_ic);
    bool calibattfix = (fixedstates_ & 1 << StateDefinition_T::q_ic);
    bool driftwvattfix = (fixedstates_ & 1 << StateDefinition_T::q_wv);
    bool driftwvposfix = (fixedstates_ & 1 << StateDefinition_T::p_wv);

    //set crosscov to zero for fixed states
    if(scalefix) state_in->clearCrossCov<StateDefinition_T::L>();
    if(calibposfix) state_in->clearCrossCov<StateDefinition_T::p_ic>();
    if(calibattfix) state_in->clearCrossCov<StateDefinition_T::q_ic>();
    if(driftwvattfix) state_in->clearCrossCov<StateDefinition_T::q_wv>();
    if(driftwvposfix) state_in->clearCrossCov<StateDefinition_T::p_wv>();

    // construct H matrix using H-blockx :-)
    // position:
    H.block<3, 3>(0, idxstartcorr_p_) = C_wv * state.get<StateDefinition_T::L>()(0); // p

    H.block<3, 3>(0, idxstartcorr_q_) = -C_wv * C_q * pci_sk * state.get<StateDefinition_T::L>()(0); // q

    H.block<3, 1>(0, idxstartcorr_L_) = scalefix ? Eigen::Matrix<double, 3, 1>::Zero() :
        (C_wv * C_q * state.get<StateDefinition_T::p_ic>()
        + C_wv * ( - state.get<StateDefinition_T::p_wv>() + state.get<StateDefinition_T::p>())).eval(); // L

    H.block<3, 3>(0, idxstartcorr_qwv_) = driftwvattfix ? Eigen::Matrix<double, 3, 3>::Zero() :
        (-C_wv * skewold).eval(); // q_wv

    H.block<3, 3>(0, idxstartcorr_pic_) = calibposfix ? Eigen::Matrix<double, 3, 3>::Zero() :
        (C_wv * C_q * state.get<StateDefinition_T::L>()(0)).eval(); //p_ic

    H.block<3, 3>(0, idxstartcorr_pwv_) = driftwvposfix ? Eigen::Matrix<double, 3, 3>::Zero() :
        ( - Eigen::Matrix<double, 3, 3>::Identity()/* * state.get<StateDefinition_T::L>()(0)*/).eval(); //p_wv

    // attitude
    H.block<3, 3>(3, idxstartcorr_q_) = C_ci; // q

    H.block<3, 3>(3, idxstartcorr_qwv_) = driftwvattfix ? Eigen::Matrix<double, 3, 3>::Zero() : (C_ci * C_q.transpose()).eval(); // q_wv

    H.block<3, 3>(3, idxstartcorr_qic_) = calibattfix ? Eigen::Matrix<double, 3, 3>::Zero() : Eigen::Matrix<double, 3, 3>::Identity().eval(); //q_ic

    //This line breaks the filter if a position sensor in the global frame is there or if we want to set a global yaw rotation.
    //H.block<1, 1>(6, idxstartcorr_qwv_ + 2) = Eigen::Matrix<double, 1, 1>::Constant(driftwvattfix ? 0.0 : 1.0); // fix vision world yaw drift because unobservable otherwise (see PhD Thesis)


  }

  /**
   * the method called by the msf_core to apply the measurement represented by this object
   */
  virtual void apply(shared_ptr<EKFState_T> state_nonconst_new, msf_core::MSF_Core<EKFState_T>& core)
  {

    if(isabsolute_){//does this measurement refer to an absolute measurement, or is is just relative to the last measurement
      const EKFState_T& state = *state_nonconst_new; //get a const ref, so we can read core states
      // init variables
      Eigen::Matrix<double, nMeasurements, msf_core::MSF_Core<EKFState_T>::nErrorStatesAtCompileTime> H_new;
      Eigen::Matrix<double, nMeasurements, 1> r_old;

      calculateH(state_nonconst_new, H_new);

      // get rotation matrices
      Eigen::Matrix<double, 3, 3> C_wv = state.get<StateDefinition_T::q_wv>().conjugate().toRotationMatrix();
      Eigen::Matrix<double, 3, 3> C_q = state.get<StateDefinition_T::q>().conjugate().toRotationMatrix();

      // construct residuals
      // position
      r_old.block<3, 1>(0, 0) = z_p_
          - (C_wv.transpose() *
              ( - state.get<StateDefinition_T::p_wv>() + state.get<StateDefinition_T::p>() + C_q.transpose() * state.get<StateDefinition_T::p_ic>()))
              * state.get<StateDefinition_T::L>();

      // attitude
      Eigen::Quaternion<double> q_err;
      q_err = (state.get<StateDefinition_T::q_wv>() * state.get<StateDefinition_T::q>() * state.get<StateDefinition_T::q_ic>()).conjugate() * z_q_;
      r_old.block<3, 1>(3, 0) = q_err.vec() / q_err.w() * 2;
      // vision world yaw drift
      q_err = state.get<StateDefinition_T::q_wv>();
      r_old(6, 0) = -2 * (q_err.w() * q_err.z() + q_err.x() * q_err.y()) / (1 - 2 * (q_err.y() * q_err.y() + q_err.z() * q_err.z()));

      if(!checkForNumeric(r_old, "r_old")){
        MSF_ERROR_STREAM("r_old: "<<r_old);
        MSF_WARN_STREAM("state: "<<const_cast<EKFState_T&>(state).toEigenVector().transpose());
      }
      if(!checkForNumeric(H_new, "H_old")){
        MSF_ERROR_STREAM("H_old: "<<H_new);
        MSF_WARN_STREAM("state: "<<const_cast<EKFState_T&>(state).toEigenVector().transpose());
      }
      if(!checkForNumeric(R_, "R_")){
        MSF_ERROR_STREAM("R_: "<<R_);
        MSF_WARN_STREAM("state: "<<const_cast<EKFState_T&>(state).toEigenVector().transpose());
      }

      // call update step in base class
      this->calculateAndApplyCorrection(state_nonconst_new, core, H_new, r_old, R_);

    }else{

      // init variables
      //get previous measurement
      shared_ptr<msf_core::MSF_MeasurementBase<EKFState_T> > prevmeas_base = core.getPreviousMeasurement(this->time, this->sensorID_);

      if(prevmeas_base->time == -1){
        MSF_WARN_STREAM("The previous measurement is invalid. Could not apply measurement! time:"<<this->time<<" sensorID: "<<this->sensorID_);
        return;
      }

      //try to make this a pose measurement
      shared_ptr<PoseMeasurement> prevmeas = dynamic_pointer_cast<PoseMeasurement>(prevmeas_base);
      if(!prevmeas){
        MSF_WARN_STREAM("The dynamic cast of the previous measurement has failed. Could not apply measurement");
        return;
      }

      //get state at previous measurement
      shared_ptr<EKFState_T> state_nonconst_old = core.getClosestState(prevmeas->time);

      if(state_nonconst_old->time == -1){
        MSF_WARN_STREAM("The state at the previous measurement is invalid. Could not apply measurement");
        return;
      }

      const EKFState_T& state_new = *state_nonconst_new; //get a const ref, so we can read core states
      const EKFState_T& state_old = *state_nonconst_old; //get a const ref, so we can read core states

      Eigen::Matrix<double, nMeasurements, msf_core::MSF_Core<EKFState_T>::nErrorStatesAtCompileTime> H_new, H_old;
      Eigen::Matrix<double, nMeasurements, 1> r_new, r_old;

      calculateH(state_nonconst_old, H_old);

      H_old *= -1;

      calculateH(state_nonconst_new, H_new);

      //TODO check that both measurements have the same states fixed!

      Eigen::Matrix<double, 3, 3> C_wv_old, C_wv_new;
      Eigen::Matrix<double, 3, 3> C_q_old, C_q_new;

      C_wv_new = state_new.get<StateDefinition_T::q_wv>().conjugate().toRotationMatrix();
      C_q_new = state_new.get<StateDefinition_T::q>().conjugate().toRotationMatrix();
      C_wv_old = state_old.get<StateDefinition_T::q_wv>().conjugate().toRotationMatrix();
      C_q_old = state_old.get<StateDefinition_T::q>().conjugate().toRotationMatrix();

      // construct residuals
      // position
      Eigen::Matrix<double, 3, 1> diffprobpos = (C_wv_new.transpose() * ( - state_new.get<StateDefinition_T::p_wv>() + state_new.get<StateDefinition_T::p>() + C_q_new.transpose() * state_new.get<StateDefinition_T::p_ic>()))
                                                     * state_new.get<StateDefinition_T::L>() -
                                                     (C_wv_old.transpose() * ( - state_old.get<StateDefinition_T::p_wv>() + state_old.get<StateDefinition_T::p>() + C_q_old.transpose() * state_old.get<StateDefinition_T::p_ic>()))
                                                     * state_old.get<StateDefinition_T::L>();

      Eigen::Matrix<double, 3, 1> diffmeaspos = z_p_ - prevmeas->z_p_;

      r_new.block<3, 1>(0, 0) = diffmeaspos - diffprobpos;

      // attitude
      Eigen::Quaternion<double> diffprobatt = (state_new.get<StateDefinition_T::q_wv>() * state_new.get<StateDefinition_T::q>() * state_new.get<StateDefinition_T::q_ic>()).conjugate() * (state_old.get<StateDefinition_T::q_wv>() * state_old.get<StateDefinition_T::q>() * state_old.get<StateDefinition_T::q_ic>());
      Eigen::Quaternion<double> diffmeasatt = z_q_.conjugate() * prevmeas->z_q_;

      Eigen::Quaternion<double> q_err;
      q_err = diffprobatt.conjugate() * diffmeasatt;

      r_new.block<3, 1>(3, 0) = q_err.vec() / q_err.w() * 2;
      // vision world yaw drift
      q_err = state_new.get<StateDefinition_T::q_wv>();
      r_new(6, 0) = -2 * (q_err.w() * q_err.z() + q_err.x() * q_err.y()) / (1 - 2 * (q_err.y() * q_err.y() + q_err.z() * q_err.z()));


      if(!checkForNumeric(r_old, "r_old")){
        MSF_ERROR_STREAM("r_old: "<<r_old);
        MSF_WARN_STREAM("state: "<<const_cast<EKFState_T&>(state_new).toEigenVector().transpose());
      }
      if(!checkForNumeric(H_new, "H_old")){
        MSF_ERROR_STREAM("H_old: "<<H_new);
        MSF_WARN_STREAM("state: "<<const_cast<EKFState_T&>(state_new).toEigenVector().transpose());
      }
      if(!checkForNumeric(R_, "R_")){
        MSF_ERROR_STREAM("R_: "<<R_);
        MSF_WARN_STREAM("state: "<<const_cast<EKFState_T&>(state_new).toEigenVector().transpose());
      }

      // call update step in base class
      this->calculateAndApplyCorrectionRelative(state_nonconst_old, state_nonconst_new, core, H_old, H_new, r_new, R_);

    }
  }
};

}
}
#endif /* POSE_MEASUREMENT_HPP_ */
