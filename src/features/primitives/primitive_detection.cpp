#include "primitive_detection.hpp"

#include <limits>

#include "parameters.hpp"
#include "logger.hpp"

//index offset of a cylinder to a plane: used for masks display purposes
const uint CYLINDER_CODE_OFFSET = 50;


//lib simplification

namespace rgbd_slam {
    namespace features {
        namespace primitives {

            Primitive_Detection::Primitive_Detection(const uint width, const uint height, const uint blocSize, const float minCosAngleForMerge, const float maxMergeDistance, const bool useCylinderDetection)
                :  
                    _histogram(blocSize), 
                    _width(width), _height(height),  
                    _pointsPerCellCount(blocSize * blocSize), 
                    _minCosAngleForMerge(minCosAngleForMerge), _maxMergeDist(maxMergeDistance),
                    _useCylinderDetection(useCylinderDetection),
                    _cellWidth(blocSize), _cellHeight(blocSize),
                    _horizontalCellsCount(_width / _cellWidth), _verticalCellsCount(_height / _cellHeight),
                    _totalCellCount(_verticalCellsCount * _horizontalCellsCount)

            {
                //Init variables
                _activationMap.assign(_totalCellCount, false);
                _unassignedMask.assign(_totalCellCount, false);
                _distancesStacked.assign(_width * _height, std::numeric_limits<float>::max());
                _segMapStacked.assign(_width * _height, 0);
                _cellDistanceTols.assign(_totalCellCount, 0.0);

                _gridPlaneSegmentMap = cv::Mat_<int>(_verticalCellsCount, _horizontalCellsCount, 0);
                _gridPlaneSegMapEroded = cv::Mat_<uchar>(_verticalCellsCount, _horizontalCellsCount, uchar(0));
                _gridCylinderSegMap = cv::Mat_<int>(_verticalCellsCount, _horizontalCellsCount, 0);
                _gridCylinderSegMapEroded = cv::Mat_<uchar>(_verticalCellsCount, _horizontalCellsCount, uchar(0));

                _distancesCellStacked = Eigen::ArrayXf::Zero(_pointsPerCellCount, 1);

                _mask = cv::Mat(_verticalCellsCount, _horizontalCellsCount, CV_8U);
                _maskEroded = cv::Mat(_verticalCellsCount, _horizontalCellsCount, CV_8U);
                _maskDilated = cv::Mat(_verticalCellsCount, _horizontalCellsCount, CV_8U);
                _maskDiff = cv::Mat(_verticalCellsCount, _horizontalCellsCount, CV_8U);

                //init kernels
                _maskSquareKernel = cv::Mat::ones(3, 3, CV_8U);
                _maskCrossKernel = cv::Mat::ones(3, 3, CV_8U);

                _maskCrossKernel.at<uchar>(0,0) = 0;
                _maskCrossKernel.at<uchar>(2,2) = 0;
                _maskCrossKernel.at<uchar>(0,2) = 0;
                _maskCrossKernel.at<uchar>(2,0) = 0;

                //array of unique_ptr<Plane_Segment>
                _planeGrid.reserve(_totalCellCount);
                for(uint i = 0; i < _totalCellCount; ++i) 
                {
                    //fill with empty nodes
                    _planeGrid.push_back(std::make_unique<Plane_Segment>(_cellWidth, _pointsPerCellCount));
                }

                //perf measurments
                resetTime = 0;
                initTime = 0;
                growTime = 0;
                mergeTime = 0;
                refineTime = 0;
                setMaskTime = 0;
            }

            void Primitive_Detection::apply_masks(const cv::Mat& inputImage, const std::vector<cv::Vec3b>& colors, const cv::Mat& maskImage, const primitive_container& primitiveSegments, const std::unordered_map<int, uint>& associatedIds, const uint bandSize, cv::Mat& labeledImage) 
            {
                assert(bandSize < _height);

                //apply masks on image
                for(uint r = bandSize; r < _height; ++r)
                {
                    const cv::Vec3b* rgbPtr = inputImage.ptr<cv::Vec3b>(r);
                    cv::Vec3b* outPtr = labeledImage.ptr<cv::Vec3b>(r);

                    for(uint c = 0; c < _width; ++c)
                    {
                        const int index = static_cast<int>(maskImage.at<uchar>(r, c)) - 1;   //get index of plane/cylinder at [r, c]

                        if(index < 0) 
                        {
                            // No primitive detected
                            outPtr[c] = rgbPtr[c];
                        }
                        else if(associatedIds.contains(index)) 
                        {    //shape associated with last frame shape
                             //there is a mask to display 
                            const uint primitiveIndex = static_cast<uint>(associatedIds.at(index));
                            if (colors.size() <= primitiveIndex) 
                            {
                                utils::log_error("Id of primitive is greater than available colors");
                            }
                            else
                            {
                                outPtr[c] = colors[primitiveIndex] * 0.5 + rgbPtr[c] * 0.5;
                            }
                        }
                        // else: not matched
                        else 
                        {
                            outPtr[c] = rgbPtr[c];
                        }
                    }
                }

                //show plane labels
                if (primitiveSegments.size() > 0)
                {
                    const uint placeInBand = bandSize * 0.75;
                    std::stringstream text1;
                    text1 << "Planes:";
                    const double planeLabelPosition = _width * 0.25;
                    cv::putText(labeledImage, text1.str(), cv::Point(planeLabelPosition, placeInBand), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255, 1));


                    std::stringstream text2;
                    text2 << "Cylinders:";
                    const double cylinderLabelPosition = _width * 0.60;
                    cv::putText(labeledImage, text2.str(), cv::Point(cylinderLabelPosition, placeInBand), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255, 1));


                    // Diplay planes and cylinders in top band
                    uint cylinderCount = 0;
                    uint planeCount = 0;
                    std::set<uint> alreadyDisplayedIds;
                    for(const std::unique_ptr<Primitive>& prim : primitiveSegments)
                    {
                        if(prim->is_matched())
                        {
                            const uint id = prim->get_id();
                            if (alreadyDisplayedIds.contains(id))
                                continue;   // already shown
                            alreadyDisplayedIds.insert(id);

                            if(id >= CYLINDER_CODE_OFFSET)
                            {
                                // primitive is a cylinder
                                const double labelPosition = _width * 0.60;
                                // make a
                                const uint labelSquareSize = bandSize * 0.5;
                                cv::rectangle(labeledImage, 
                                        cv::Point(labelPosition + 80 + placeInBand * cylinderCount, 6),
                                        cv::Point(labelPosition + 80 + labelSquareSize + placeInBand * cylinderCount, 6 + labelSquareSize), 
                                        cv::Scalar(
                                            colors[id][0],
                                            colors[id][1],
                                            colors[id][2]),
                                        -1);
                                ++cylinderCount;
                            }
                            else
                            {
                                const double labelPosition = _width * 0.25;
                                // make a
                                const uint labelSquareSize = bandSize * 0.5;
                                cv::rectangle(labeledImage, 
                                        cv::Point(labelPosition + 80 + placeInBand * planeCount, 6),
                                        cv::Point(labelPosition + 80 + labelSquareSize + placeInBand * planeCount, 6 + labelSquareSize), 
                                        cv::Scalar(
                                            colors[id][0],
                                            colors[id][1],
                                            colors[id][2]),
                                        -1);
                                ++planeCount;
                            }
                        }
                        //else: not matched
                    }
                }
            }



            /*
             * Find the planes in the organized depth matrix using region growing
             * Segout will contain a 2D representation of the planes
             */
            void Primitive_Detection::find_primitives(const Eigen::MatrixXf& depthMatrix, primitive_container& primitiveSegments, cv::Mat& segOut) 
            {
                //reset used data structures
                reset_data();

                double t1 = cv::getTickCount();
                //init planar grid
                init_planar_cell_fitting(depthMatrix);
                double td = (cv::getTickCount() - t1) / (double)cv::getTickFrequency();
                resetTime += td;

                //init and fill histogram
                t1 = cv::getTickCount();
                const uint remainingPlanarCells = init_histogram();
                td = (cv::getTickCount() - t1) / (double)cv::getTickFrequency();
                initTime += td;


                t1 = cv::getTickCount();
                intpair_vector cylinder2regionMap;
                grow_planes_and_cylinders(remainingPlanarCells, cylinder2regionMap);
                td = (cv::getTickCount() - t1) / (double)cv::getTickFrequency();
                growTime += td;

                //merge sparse planes
                uint_vector planeMergeLabels;
                merge_planes(planeMergeLabels);

                t1 = cv::getTickCount();
                //refine planes boundaries and fill the final planes vector
                refine_plane_boundaries(depthMatrix, planeMergeLabels, primitiveSegments);
                td = (cv::getTickCount() - t1) / (double)cv::getTickFrequency();
                mergeTime += td;

                if(_useCylinderDetection) 
                {
                    t1 = cv::getTickCount();
                    //refine cylinders boundaries and fill the final cylinders vector
                    refine_cylinder_boundaries(depthMatrix, cylinder2regionMap, primitiveSegments); 
                    td = (cv::getTickCount() - t1) / (double)cv::getTickFrequency();
                    refineTime += td;
                }

                t1 = cv::getTickCount();
                //set mask image
                set_masked_display(segOut);
                td = (cv::getTickCount() - t1) / (double)cv::getTickFrequency();
                setMaskTime += td;
            }

            void Primitive_Detection::reset_data() 
            {
                _histogram.reset();

                //planeGrid SHOULD NOT be cleared
                _planeSegments.clear();
                _cylinderSegments.clear();

                _gridPlaneSegmentMap = 0;
                _gridPlaneSegMapEroded = 0;
                _gridCylinderSegMap = 0;
                _gridCylinderSegMapEroded = 0;

                _distancesCellStacked.setZero();

                //reset stacked distances
                //activation map do not need to be cleared
                std::fill_n(_unassignedMask.begin(), _unassignedMask.size(), false);
                std::fill_n(_distancesStacked.begin(), _distancesStacked.size(), std::numeric_limits<float>::max());
                std::fill_n(_segMapStacked.begin(), _segMapStacked.size(), 0);
                std::fill_n(_cellDistanceTols.begin(), _cellDistanceTols.size(), 0.0);

                //mat masks do not need to be cleared
                //kernels should not be cleared
            }

            void Primitive_Detection::init_planar_cell_fitting(const Eigen::MatrixXf& depthCloudArray) 
            {
                float sinCosAngleForMerge = sqrt(1 - pow(_minCosAngleForMerge, 2));

                //for each planeGrid cell
                const size_t planeGridSize = _planeGrid.size();
                for(size_t stackedCellId = 0; stackedCellId < planeGridSize; ++stackedCellId) {
                    //init the plane grid cell
                    _planeGrid[stackedCellId]->init_plane_segment(depthCloudArray, stackedCellId);

                    if (_planeGrid[stackedCellId]->is_planar()) {
                        const uint cellDiameter = (
                                depthCloudArray.block(stackedCellId * _pointsPerCellCount + _pointsPerCellCount - 1, 0, 1, 3) - 
                                depthCloudArray.block(stackedCellId * _pointsPerCellCount, 0, 1, 3)
                                ).norm(); 

                        //array of depth metrics: neighbors merging threshold
                        _cellDistanceTols[stackedCellId] = pow(std::clamp(cellDiameter * sinCosAngleForMerge, 20.0f, _maxMergeDist), 2);
                    }
                }
            }

            uint Primitive_Detection::init_histogram() 
            {
                uint remainingPlanarCells = 0;
                Eigen::MatrixXd histBins(_totalCellCount, 2);

                const size_t planeGridSize = _planeGrid.size();
                for(uint cellId = 0; cellId < planeGridSize; ++cellId) {  
                    if(_planeGrid[cellId]->is_planar()) {
                        const vector3& planeNormal = _planeGrid[cellId]->get_normal();
                        const double nx = planeNormal.x();
                        const double ny = planeNormal.y();

                        const double projNormal = 1 / sqrt(nx * nx + ny * ny);
                        histBins(cellId, 0) = acos( -planeNormal.z());
                        histBins(cellId, 1) = atan2(nx * projNormal, ny * projNormal);
                        remainingPlanarCells += 1;
                        _unassignedMask[cellId] = true; 
                    }
                }
                _histogram.init_histogram(histBins, _unassignedMask);
                return remainingPlanarCells;
            }

            void Primitive_Detection::grow_planes_and_cylinders(uint remainingPlanarCells, intpair_vector& cylinder2regionMap) 
            {
                uint cylinderCount = 0;
                std::vector<uint> seedCandidates;
                //find seed planes and make them grow
                while(remainingPlanarCells > 0) 
                {
                    //get seed candidates
                    seedCandidates.clear();
                    _histogram.get_points_from_most_frequent_bin(seedCandidates);

                    if (seedCandidates.size() < Parameters::get_minimum_plane_seed_count())
                        break;

                    //select seed cell with min MSE
                    uint seedId = 0;    //should not necessarily stay to 0 after the loop
                    float minMSE = INT_MAX;
                    for(uint i = 0; i < seedCandidates.size(); ++i) 
                    {
                        const uint seedCandidate = seedCandidates[i];
                        if(_planeGrid[seedCandidate]->get_MSE() < minMSE) 
                        {
                            seedId = static_cast<int>(seedCandidate);
                            minMSE = _planeGrid[seedCandidate]->get_MSE();
                            //if(minMSE <= 0)
                            //    break;
                        }
                    }

                    //copy plane segment in new object
                    Plane_Segment newPlaneSegment(*_planeGrid[seedId]);

                    //Seed cell growing
                    uint y = static_cast<uint>(seedId / _horizontalCellsCount);
                    uint x = static_cast<uint>(seedId % _horizontalCellsCount);

                    //activationMap set to false
                    std::fill_n(_activationMap.begin(), _activationMap.size(), false);
                    //grow plane region
                    region_growing(x, y, newPlaneSegment.get_normal(), newPlaneSegment.get_plane_d());

                    const size_t activationMapSize = _activationMap.size();
                    assert(activationMapSize == _unassignedMask.size());
                    assert(activationMapSize == _planeGrid.size());

                    //merge activated cells & remove them from histogram
                    uint cellActivatedCount = 0;
                    for(uint i = 0; i < activationMapSize; ++i) 
                    {
                        if(_activationMap[i]) 
                        {
                            newPlaneSegment.expand_segment(_planeGrid[i]);
                            cellActivatedCount += 1;
                            _histogram.remove_point(i);
                            _unassignedMask[i] = false;
                            remainingPlanarCells -= 1;
                        }
                    }

                    if(cellActivatedCount < Parameters::get_minimum_cell_activated()) 
                    {
                        _histogram.remove_point(seedId);
                        continue;
                    }

                    //fit plane to merged data
                    newPlaneSegment.fit_plane();

                    if(not _useCylinderDetection or newPlaneSegment.get_score() > 100) 
                    {
                        //its certainly a plane or we ignore cylinder detection
                        _planeSegments.push_back(std::make_unique<Plane_Segment>(newPlaneSegment));
                        const size_t currentPlaneCount = _planeSegments.size();
                        //mark cells
                        uint i = 0;
                        for(uint r = 0; r < _verticalCellsCount; ++r) 
                        {
                            int* row = _gridPlaneSegmentMap.ptr<int>(r);
                            for(uint c = 0; c < _horizontalCellsCount; ++c)
                            {
                                assert(i < activationMapSize);

                                if(_activationMap[i])
                                    row[c] = currentPlaneCount;

                                i += 1;
                            }
                        }

                    }
                    else if(_useCylinderDetection and cellActivatedCount > 5) 
                    {
                        //cylinder fitting
                        // It is an extrusion
                        _cylinderSegments.push_back(std::make_unique<Cylinder_Segment>(_planeGrid, _activationMap, cellActivatedCount));
                        const cylinder_segment_unique_ptr& cy = _cylinderSegments.back();

                        // Fit planes to subsegments
                        for(uint segId = 0; segId < cy->get_segment_count(); ++segId)
                        {
                            newPlaneSegment.clear_plane_parameters();
                            for(uint c = 0; c < cellActivatedCount; ++c)
                            {
                                if (cy->is_inlier_at(segId, c))
                                {
                                    const uint localMapIndex = cy->get_local_to_global_mapping(c);
                                    assert(localMapIndex < _planeGrid.size());

                                    newPlaneSegment.expand_segment(_planeGrid[localMapIndex]);
                                }
                            }

                            newPlaneSegment.fit_plane();
                            // Model selection based on MSE
                            if(newPlaneSegment.get_MSE() < cy->get_MSE_at(segId))
                            {
                                //MSE of the plane is less than MSE of the cylinder + this plane 
                                _planeSegments.push_back(std::make_unique<Plane_Segment>(newPlaneSegment));
                                const uint currentPlaneCount = _planeSegments.size();
                                for(uint c = 0; c < cellActivatedCount; ++c)
                                {
                                    if (cy->is_inlier_at(segId, c))
                                    {
                                        uint cellId = cy->get_local_to_global_mapping(c);
                                        _gridPlaneSegmentMap.at<int>(cellId / _horizontalCellsCount, cellId % _horizontalCellsCount) = currentPlaneCount;
                                    }
                                }
                            }
                            else 
                            {
                                cylinderCount += 1;
                                cylinder2regionMap.push_back(std::make_pair(_cylinderSegments.size() - 1, segId));
                                for(uint c = 0; c < cellActivatedCount; ++c)
                                {
                                    if (cy->is_inlier_at(segId, c))
                                    {
                                        int cellId = cy->get_local_to_global_mapping(c);
                                        _gridCylinderSegMap.at<int>(cellId / _horizontalCellsCount, cellId % _horizontalCellsCount) = cylinderCount;
                                    }
                                }
                            }
                        }
                    }
                }//\while
            }

            void Primitive_Detection::merge_planes(uint_vector& planeMergeLabels) 
            {
                const uint planeCount = _planeSegments.size();

                Matrixb planesAssocMat = Matrixb::Constant(planeCount, planeCount, false);
                get_connected_components(_gridPlaneSegmentMap, planesAssocMat);

                for(uint i = 0; i < planeCount; ++i)
                    planeMergeLabels.push_back(i);

                for(uint r = 0; r < planesAssocMat.rows(); ++r) 
                {
                    bool planeWasExpanded = false;
                    const uint planeId = planeMergeLabels[r];
                    const plane_segment_unique_ptr& testPlane = _planeSegments[planeId];
                    const vector3& testPlaneNormal = testPlane->get_normal();

                    for(uint c = r+1; c < planesAssocMat.cols(); ++c) 
                    {
                        if(planesAssocMat(r, c)) 
                        {
                            const plane_segment_unique_ptr& mergePlane = _planeSegments[c];
                            const vector3& mergePlaneNormal = mergePlane->get_normal();
                            const double cosAngle = testPlaneNormal.dot(mergePlaneNormal);

                            const vector3& mergePlaneMean = mergePlane->get_mean();
                            const double distance = pow(
                                    testPlaneNormal.dot(mergePlaneMean) + testPlane->get_plane_d(),
                                    2);

                            if(cosAngle > _minCosAngleForMerge and distance < _maxMergeDist) 
                            {
                                //merge plane segments
                                _planeSegments[planeId]->expand_segment(mergePlane);
                                planeMergeLabels[c] = planeId;
                                planeWasExpanded = true;
                            }
                            else 
                            {
                                planesAssocMat(r, c) = false;
                            }
                        }
                    }
                    if(planeWasExpanded)    //plane was merged with other planes
                        _planeSegments[planeId]->fit_plane();
                }
            }

            void Primitive_Detection::refine_plane_boundaries(const Eigen::MatrixXf& depthCloudArray, uint_vector& planeMergeLabels, primitive_container& primitiveSegments) 
            {
                //refine the coarse planes boundaries to smoother versions
                const uint planeCount = _planeSegments.size();
                uint planeIdAllocator = 0;
                for(uint i = 0; i < planeCount; ++i) 
                {
                    if (i != planeMergeLabels[i])
                        continue;

                    _mask = cv::Scalar(0);
                    for(uint j = i; j < planeCount; ++j) 
                    {
                        if(planeMergeLabels[j] == planeMergeLabels[i])
                            _mask.setTo(1, _gridPlaneSegmentMap == (j + 1));
                    }

                    cv::erode(_mask, _maskEroded, _maskCrossKernel);
                    double min, max;
                    cv::minMaxLoc(_maskEroded, &min, &max);

                    if(max <= 0)    //completely eroded
                        continue;

                    cv::dilate(_mask, _maskDilated, _maskSquareKernel);
                    _maskDiff = _maskDilated - _maskEroded;

                    // new plane ID
                    const uchar planeId = ++planeIdAllocator;
                    assert(planeId < CYLINDER_CODE_OFFSET);

                    //add new plane to final shapes
                    primitiveSegments.push_back(std::move(std::make_unique<Plane>(_planeSegments[i], planeId - 1, _maskDilated)));

                    const vector3& planeNormal = _planeSegments[i]->get_normal();
                    const float nx = planeNormal.x();
                    const float ny = planeNormal.y();
                    const float nz = planeNormal.z();
                    const float d = _planeSegments[i]->get_plane_d();
                    //TODO: better distance metric
                    const float maxDist = 9 * _planeSegments[i]->get_MSE();

                    _gridPlaneSegMapEroded.setTo(planeId, _maskEroded > 0);

                    //cell refinement
                    for(uint cellR = 0, stackedCellId = 0; cellR < _verticalCellsCount; ++cellR) 
                    {
                        const uchar* rowPtr = _maskDiff.ptr<uchar>(cellR);

                        for(uint cellC = 0; cellC < _horizontalCellsCount; ++cellC, ++stackedCellId) 
                        {
                            const uint offset = stackedCellId * _pointsPerCellCount;
                            const uint nextOffset = offset + _pointsPerCellCount;

                            if(rowPtr[cellC] > 0) 
                            {
                                //compute distance block
                                _distancesCellStacked = 
                                    depthCloudArray.block(offset, 0, _pointsPerCellCount, 1).array() * nx +
                                    depthCloudArray.block(offset, 1, _pointsPerCellCount, 1).array() * ny +
                                    depthCloudArray.block(offset, 2, _pointsPerCellCount, 1).array() * nz +
                                    d;

                                //Assign pixel
                                for(uint pt = offset, j = 0; pt < nextOffset; ++j, ++pt) 
                                {
                                    float dist = pow(_distancesCellStacked(j), 2);
                                    if(dist < maxDist and dist < _distancesStacked[pt]) 
                                    {
                                        _distancesStacked[pt] = dist;
                                        _segMapStacked[pt] = planeId;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            void Primitive_Detection::refine_cylinder_boundaries(const Eigen::MatrixXf& depthCloudArray, intpair_vector& cylinderToRegionMap, primitive_container& primitiveSegments) 
            {
                if(not _useCylinderDetection)
                    return; //no cylinder detections

                uint cylinderIdAllocator = CYLINDER_CODE_OFFSET;
                for(uint i = 0; i < cylinderToRegionMap.size(); i++)
                {
                    // Build mask
                    _mask = cv::Scalar(0);
                    _mask.setTo(1, _gridCylinderSegMap == (i + 1));

                    // Erode to obtain borders
                    cv::erode(_mask, _maskEroded, _maskCrossKernel);
                    double min, max;
                    cv::minMaxLoc(_maskEroded, &min, &max);

                    // If completely eroded ignore cylinder
                    if (max <= 0)
                        continue;

                    // Affect a new cylinder id
                    const uchar cylinderId = ++cylinderIdAllocator;

                    // Dilate to obtain borders
                    cv::dilate(_mask, _maskDilated, _maskSquareKernel);
                    _maskDiff = _maskDilated - _maskEroded;

                    _gridCylinderSegMapEroded.setTo(cylinderId, _maskEroded > 0);

                    const int regId = cylinderToRegionMap[i].first;
                    const int subRegId = cylinderToRegionMap[i].second;
                    const cylinder_segment_unique_ptr& cylinderSegRef = _cylinderSegments[regId];

                    //add new cylinder to final shapes
                    primitiveSegments.push_back(std::move(std::make_unique<Cylinder>(cylinderSegRef, cylinderId - 1, _maskDilated)));


                    // Get variables needed for point-surface distance computation
                    const vector3& P2 = cylinderSegRef->get_axis2_point(subRegId);
                    const vector3& P1P2 = P2 - cylinderSegRef->get_axis1_point(subRegId);
                    const double P1P2Normal = cylinderSegRef->get_axis_normal(subRegId);
                    const double radius = cylinderSegRef->get_radius(subRegId);
                    const double maxDist = 9 * cylinderSegRef->get_MSE_at(subRegId);

                    // Cell refinement
                    for(uint cellR = 0, stackedCellId = 0; cellR < _verticalCellsCount; cellR += 1)
                    {
                        const uchar* rowPtr = _maskDiff.ptr<uchar>(cellR);
                        for(uint cellC = 0; cellC < _horizontalCellsCount; cellC++, stackedCellId++) 
                        {
                            const uint offset = stackedCellId * _pointsPerCellCount;
                            const uint nextOffset = offset + _pointsPerCellCount;
                            if(rowPtr[cellC] > 0){
                                // Update cells
                                for(uint pt = offset, j = 0; pt < nextOffset; pt++, j++) 
                                {
                                    const vector3& point = depthCloudArray.row(pt).cast<double>();
                                    if(point.z() > 0)
                                    {
                                        double dist = pow(P1P2.cross(point - P2).norm() / P1P2Normal - radius, 2);
                                        if(dist < maxDist and dist < _distancesStacked[pt])
                                        {
                                            _distancesStacked[pt] = dist;
                                            _segMapStacked[pt] = cylinderId;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            void Primitive_Detection::set_masked_display(cv::Mat& segOut) 
            {
                //copy and rearranging
                // Copy inlier list to matrix form
                for(uint cellR = 0; cellR < _verticalCellsCount; cellR += 1)
                {
                    const uchar* gridPlaneErodedRowPtr = _gridPlaneSegMapEroded.ptr<uchar>(cellR);
                    const uchar* gridCylinderErodedRowPtr = _gridCylinderSegMapEroded.ptr<uchar>(cellR);
                    const uint rOffset = cellR * _cellHeight;
                    const uint rLimit = rOffset + _cellHeight;

                    for(uint cellC = 0; cellC < _horizontalCellsCount; cellC += 1)
                    {
                        const uint cOffset = cellC * _cellWidth;

                        if (gridPlaneErodedRowPtr[cellC] > 0)
                        {
                            // Set rectangle equal to assigned cell
                            segOut(cv::Rect(cOffset, rOffset, _cellWidth, _cellHeight)).setTo(gridPlaneErodedRowPtr[cellC]);
                        } 
                        else if(gridCylinderErodedRowPtr[cellC] > 0) 
                        {
                            // Set rectangle equal to assigned cell
                            segOut(cv::Rect(cOffset, rOffset, _cellWidth, _cellHeight)).setTo(gridCylinderErodedRowPtr[cellC]);
                        }
                        else 
                        {
                            const uint cLimit = cOffset + _cellWidth;
                            // Set cell pixels one by one
                            const uchar* stackPtr = &_segMapStacked[_pointsPerCellCount * cellR * _horizontalCellsCount + _pointsPerCellCount * cellC];
                            for(uint r = rOffset, i = 0; r < rLimit; r++)
                            {
                                uchar* rowPtr = segOut.ptr<uchar>(r);
                                for(uint c = cOffset; c < cLimit; c++, i++)
                                {
                                    const uchar id = stackPtr[i];
                                    if(id > 0) 
                                        rowPtr[c] = id;
                                }
                            }
                        }
                    }
                }
            }


            void Primitive_Detection::get_connected_components(const cv::Mat& segmentMap, Matrixb& planesAssociationMatrix) const 
            {
                const uint rows2scanCount = segmentMap.rows - 1;
                const uint cols2scanCount = segmentMap.cols - 1;

                for(uint r = 0; r < rows2scanCount; r += 1) 
                {
                    const int *row = segmentMap.ptr<int>(r);
                    const int *rowBelow = segmentMap.ptr<int>(r + 1);
                    for(uint c = 0; c < cols2scanCount; c += 1) 
                    {
                        const int pixelValue = row[c];
                        if(pixelValue > 0) 
                        {
                            if(row[c + 1] > 0 and pixelValue != row[c + 1]) 
                            {
                                planesAssociationMatrix(pixelValue - 1, row[c + 1] - 1) = true;
                                planesAssociationMatrix(row[c + 1] - 1, pixelValue - 1) = true;
                            }
                            if(rowBelow[c] > 0 and pixelValue != rowBelow[c]) 
                            {
                                planesAssociationMatrix(pixelValue - 1, rowBelow[c] - 1) = true;
                                planesAssociationMatrix(rowBelow[c] - 1, pixelValue - 1) = true;
                            }
                        }
                    }
                }
            }


            void Primitive_Detection::region_growing(const unsigned short x, const unsigned short y, const vector3& seedPlaneNormal, const double seedPlaneD) 
            {
                assert(_activationMap.size() == _unassignedMask.size());

                const size_t index = x + _horizontalCellsCount * y;
                if (index >= _totalCellCount)
                    return;

                assert(index < _activationMap.size());
                assert(index < _unassignedMask.size());
                if ((not _unassignedMask[index]) or _activationMap[index]) 
                    //pixel is not part of a component or already labelled
                    return;

                assert(index < _planeGrid.size()); 

                const vector3& secPlaneNormal = _planeGrid[index]->get_normal();
                const vector3& secPlaneMean = _planeGrid[index]->get_mean();
                const double secPlaneD = _planeGrid[index]->get_plane_d();

                if (
                        //_planeGrid[index]->is_depth_discontinuous(secPlaneMean) or 
                        seedPlaneNormal.dot(secPlaneNormal) < _minCosAngleForMerge
                        or pow(seedPlaneNormal.dot(secPlaneMean) + seedPlaneD, 2) > _cellDistanceTols[index]
                   )//angle between planes < threshold or dist between planes > threshold
                    return;

                _activationMap[index] = true;

                // Now label the 4 neighbours:
                if (x > 0)
                    region_growing(x - 1, y, secPlaneNormal, secPlaneD);   // left  pixel
                if (x < _width - 1)  
                    region_growing(x + 1, y, secPlaneNormal, secPlaneD);  // right pixel
                if (y > 0)        
                    region_growing(x, y - 1, secPlaneNormal, secPlaneD);   // upper pixel 
                if (y < _height - 1) 
                    region_growing(x, y + 1, secPlaneNormal, secPlaneD);   // lower pixel
            }



            Primitive_Detection::~Primitive_Detection() 
            {
            }

        }
    }
}

