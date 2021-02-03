#ifndef POSE_UTILS_HPP
#define POSE_UTILS_HPP

#include "Pose.hpp"

namespace poseEstimation {

    class Pose_Utils {
        public:
            static Pose compute_right_camera_pose(const Pose &leftCamPose, double baseLine);

            static matrix34 compute_world_to_camera_transform(const Pose& cameraPose);

            static matrix34 compute_projection_matrix(const Pose& cameraPose, const matrix33& intrisics);

            static vector2 project_point(const vector3 &pt, const matrix34& projectionMatrix);
    };
}


#endif
