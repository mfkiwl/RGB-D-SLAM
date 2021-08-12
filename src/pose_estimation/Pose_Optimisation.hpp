#ifndef POSE_OPTIMISATION_HPP
#define POSE_OPTIMISATION_HPP 

#include "types.hpp"

// types
#include "KeyPointDetection.hpp"

#include <Eigen/Eigen>
#include <unsupported/Eigen/NonLinearOptimization>

#include <iostream>

namespace rgbd_slam {
namespace poseOptimisation {



    /**
     * \brief Structure given to the Levenberg-Marquardt algorithm. It optimizes a rotation (quaternion) and a translation (vector3) using the matched features from a frame to the local map, using their distances to one another as the main metric.
     *
     */
    template<typename _Scalar, int NX = Eigen::Dynamic, int NY = Eigen::Dynamic>
        struct Levenberg_Marquard_Functor {

            //tell the called the numerical type and input/ouput size
            typedef _Scalar Scalar;
            enum {
                InputsAtCompileTime = NX,
                ValuesAtCompileTime = NY
            };

            // typedefs for the original functor
            typedef Eigen::Matrix<Scalar, InputsAtCompileTime, 1> InputType;
            typedef Eigen::Matrix<Scalar, ValuesAtCompileTime, 1> ValueType;
            typedef Eigen::Matrix<Scalar, ValuesAtCompileTime, InputsAtCompileTime> JacobianType;

            Levenberg_Marquard_Functor(unsigned int inputCount, unsigned int outputCount) :
                _M(inputCount), _N(outputCount)
            {
            }

            unsigned int values() const {
                return _N;
            }
            unsigned int inputs() const {
                return _M;
            }

            unsigned int _M;
            unsigned int _N;
        };


    /**
     * \brief Implementation of the main pose and orientation optimisation, to be used by the Levenberg Marquard optimisator
     */
    struct Pose_Estimator: Levenberg_Marquard_Functor<double> 
    {
        // Simple constructor
        Pose_Estimator(unsigned int n, const matched_point_container& points);

        // Implementation of the objective function
        int operator()(const Eigen::VectorXd& z, Eigen::VectorXd& fvec) const;

        private:
        matched_point_container _points; 
    };

    struct Pose_Functor : Eigen::NumericalDiff<Pose_Estimator> {};




    /**
     * \brief Use for debug.
     * \return Returns a string with the human readable version of Eigen LevenbergMarquardt output status
     */
    const std::string get_human_readable_end_message(Eigen::LevenbergMarquardtSpace::Status status);

}       /* poseOptimisation*/
}

#endif
