#ifndef RGBDSLAM_MAPMANAGEMENT_MAPRIMITIVE_HPP
#define RGBDSLAM_MAPMANAGEMENT_MAPRIMITIVE_HPP

#include "shape_primitives.hpp"

namespace rgbd_slam {
    namespace map_management {

        const uchar UNMATCHED_PRIMITIVE_ID = 0;

        struct MatchedPrimitive 
        {
            MatchedPrimitive():
                _matchId(UNMATCHED_PRIMITIVE_ID)
            {};

            bool is_matched() const
            {
                return _matchId != UNMATCHED_PRIMITIVE_ID;
            }

            void mark_unmatched()
            {
                _matchId = UNMATCHED_PRIMITIVE_ID;
            }

            uchar _matchId;
        };

        struct Primitive 
        {
            Primitive(features::primitives::primitive_uniq_ptr primitive): 
                _id(_currentPrimitiveId++),
                _primitive(std::move(primitive))
            {
                cv::Vec3b color;
                color[0] = rand() % 255;
                color[1] = rand() % 255;
                color[2] = rand() % 255;
                _color = color;
            };

            // Unique identifier of this primitive in map
            const size_t _id;

            const features::primitives::primitive_uniq_ptr _primitive;
            MatchedPrimitive _matchedPrimitive;

            cv::Scalar _color;  // display color of this primitive


            private:
            inline static size_t _currentPrimitiveId = 1;   // 0 is invalid
        };



    }   // map_management
}       // rgbd_slam



#endif
