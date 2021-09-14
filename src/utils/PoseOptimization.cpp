#include "PoseOptimization.hpp"

#include "utils.hpp"
#include "LevenbergMarquardFunctors.hpp"
#include "parameters.hpp"

namespace rgbd_slam {
    namespace utils {

        void Pose_Optimization::compute_optimized_pose(poseEstimation::Pose& currentPose, match_point_container& matchedPoints)
        {
            // Vector to optimize: (0, 1, 2) is delta position, (3, 4, 5) is delta rotation as a simplified quaternion (parameter W is sqrt(1 - x² - y² - z²) (works for small angles)
            Eigen::VectorXd input(6);
            // 3D pose
            input[0] = 0;
            input[1] = 0;
            input[2] = 0;
            // X Y Z of a quaternion
            input[3] = 0;
            input[4] = 0;
            input[5] = 0;

            // Optimize function 
            Pose_Functor pose_optimisation_functor(
                    Pose_Estimator(
                        input.size(), 
                        matchedPoints, 
                        currentPose.get_position(),
                        currentPose.get_orientation_quaternion()
                        )
                    );
            // Optimization algorithm
            Eigen::LevenbergMarquardt<Pose_Functor, double> poseOptimisator( pose_optimisation_functor );

            // xtol     : tolerance for the norm of the solution vector
            // ftol     : tolerance for the norm of the vector function
            // gtol     : tolerance for the norm of the gradient of the error function
            // factor   : step bound for the diagonal shift
            // epsfcn   : error precision
            // maxfev   : maximum number of function evaluation
            poseOptimisator.parameters.maxfev = Parameters::get_maximum_optimization_iterations();

            const Eigen::LevenbergMarquardtSpace::Status endStatus = poseOptimisator.minimize(input);

            const quaternion& endRotation = get_underparametrized_quaternion(input[3], input[4], input[5]);
            const vector3 endTranslation(input[0], input[1], input[2]);

            if (endStatus == Eigen::LevenbergMarquardtSpace::Status::TooManyFunctionEvaluation)
            {
                // Error: reached end of minimization without reaching a minimum
                const std::string message = get_human_readable_end_message(endStatus);
                std::cerr << matchedPoints.size() << " pts " << endTranslation.transpose() << " in " << poseOptimisator.iter << " iters. Result " << endStatus << " (" << message << ")" << std::endl;
            }

            // Update refine pose with optimized pose
            currentPose.update(endTranslation, endRotation);
        }


    }   /* utils */
}   /* rgbd_slam */
