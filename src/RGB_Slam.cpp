#include "RGB_Slam.hpp"

namespace poseEstimation {


    RGB_SLAM::RGB_SLAM(const Parameters &params) :
        _params(params),
        _featureHandler(_params),
        _localMap(_params, &_featureHandler),
        _pnpSolver(_params.get_fx(), _params.get_fy(), _params.get_cx(), _params.get_cy(), _params.get_baseline())

    {
        reset();

        triangulationPolicy = &RGB_SLAM::triangulation_policy_decreasing_matches;
        if(_params.get_triangulation_policy() == Parameters::etriangulation_policy_always_triangulate)
            triangulationPolicy = &RGB_SLAM::triangulation_policy_always_triangulate;
        else if(_params.get_triangulation_policy() == Parameters::etriangulation_policy_map_size)
            triangulationPolicy = &RGB_SLAM::triangulation_policy_map_size;
    }

    void RGB_SLAM::reset() {
        _localMap.reset();
        _motionModel.reset();

        _lastPose = Pose();
        _frameNumber = 0;
        _lastMatches = std::deque<long unsigned int>(N_MATCHES_WINDOWS, std::numeric_limits<long unsigned int>::max());
        _state = eState_NOT_INITIALIZED;

    }


    /*
     *  Update the current pose from features and motion model
     *
     *  in: imgRGB
     *  in: imgDepth
     *
     * returns the new pose
     */
    Pose RGB_SLAM::track(const cv::Mat& imgRGB, const cv::Mat& imgDepth) {
        _frameNumber += 1;

        if(_state == eState_LOST) {
            return _lastPose;
        }

        //In RGBD case, imgRGB must be the grayscale image and imgDepth the depth with format CV_32F
        assert(imgRGB.channels() == 1 and imgDepth.type() == CV_32F);

        Image_Features_Struct features;
        _featureHandler.compute_features(imgRGB, imgDepth, features);

        if(_state == eState_NOT_INITIALIZED) {
            Pose identityPose;
            _localMap.update_with_new_triangulation(identityPose, features, true);
            _state = eState_TRACKING;
            _lastMatches[0] = _localMap.get_map_size();
            return identityPose;
        }

        bool isTracking = false;
        Pose predictedPose = _motionModel.predict_next_pose(_lastPose);
        Pose computedPose = perform_tracking(predictedPose, features, isTracking);

        if(not isTracking) {
            _state = eState_LOST;
            return _lastPose;
        }

        _lastPose = computedPose;
        return computedPose;
    }

    /*
     *   Refine the pose estimated from motion model and decide if tracking is lost
     *
     * in estimatedPose Pose estimated from motion model
     * in features Detected features in the image
     * out isTracking Pose estimator not lost
     *
     */
    Pose RGB_SLAM::perform_tracking(const Pose& estimatedPose, Image_Features_Struct& features, bool& isTracking) {

        vector3_array matchedPoints;    //matched points
        std::vector<int> matchOutliers; //unmatched points

        _localMap.find_matches(estimatedPose, features, matchedPoints, matchOutliers);
        const long unsigned int matchesCnt = matchedPoints.size();

        std::cout << features.get_features_count() << " : " << matchesCnt << std::endl;
        if(matchesCnt < _params.get_min_matches_for_tracking()) {
            //not enough matched features
            std::cout << "Not enough features matched for tracking: only " << matchesCnt << " matches" << std::endl;
            isTracking = false;
            return _lastPose;
        }

        _lastMatches.push_back(matchesCnt);
        _lastMatches.pop_front();

        Pose optimizedPose = _pnpSolver.compute_pose(estimatedPose, features, matchedPoints, matchOutliers);

        //remove untracked points
        _localMap.clean_untracked_points(features);

        if (_params.get_staged_threshold() > 0)
        {
            _localMap.update_staged_map_points(optimizedPose, features);
        }

        if (need_new_triangulation())
        {
            _localMap.update_with_new_triangulation(optimizedPose, features);
        }

        isTracking = true;
        return optimizedPose;
    }

    bool RGB_SLAM::need_new_triangulation()
    {
        return ((this->*triangulationPolicy)());
    }

    bool RGB_SLAM::triangulation_policy_decreasing_matches()
    {
        const float ratio = 0.99;
        for (int i = N_MATCHES_WINDOWS - 1; i > 0; --i)
        {
            if (static_cast<float>(_lastMatches[i]) > ratio * static_cast<float>(_lastMatches[i - 1]))
            {
                return false;
            }
        }
        return true;
    }

    bool RGB_SLAM::triangulation_policy_always_triangulate()
    {
        return true;
    }

    bool RGB_SLAM::triangulation_policy_map_size()
    {
        return (_localMap.get_map_size() < 1000);
    }







}
