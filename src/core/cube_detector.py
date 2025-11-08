import numpy as np
import open3d as o3d
from typing import List, Tuple, Optional
from .point_cloud_processor import PointCloudProcessor
from .plane_detector import PlaneDetector

class CubeDetector:
    def __init__(self):
        self.point_cloud_processor = PointCloudProcessor()
        self.plane_detector = PlaneDetector()
    
    def detect_cube_from_point_cloud(self, pcd: o3d.geometry.PointCloud) -> Optional[Tuple[np.ndarray, np.ndarray]]:
        """Detect cube in a point cloud using plane segmentation and geometric analysis"""
        try:
            # Downsample and estimate normals
            downsampled_pcd = self.point_cloud_processor.downsample_point_cloud(pcd)
            downsampled_pcd = self.point_cloud_processor.estimate_normals(downsampled_pcd)
            
            # Find the largest plane (likely the surface the cube is on)
            plane_model, inliers = self.point_cloud_processor.segment_plane(downsampled_pcd)
            
            # Extract the plane and objects above it
            inlier_cloud = downsampled_pcd.select_by_index(inliers)
            outlier_cloud = downsampled_pcd.select_by_index(inliers, invert=True)
            
            # Find the cube by looking for geometric features
            # For simplicity, we'll assume the cube is the largest object above the plane
            if len(outlier_cloud.points) > 0:
                # Get bounding box of the outlier points
                bbox = outlier_cloud.get_axis_aligned_bounding_box()
                
                # Calculate cube dimensions
                min_bound = bbox.get_min_bound()
                max_bound = bbox.get_max_bound()
                dimensions = max_bound - min_bound
                
                # Calculate center position
                center = (min_bound + max_bound) / 2
                
                return center, dimensions
            
            return None
            
        except Exception as e:
            print(f"Error detecting cube: {e}")
            return None
    
    def detect_cube_from_image(self, image: np.ndarray) -> Optional[List[Tuple[int, int]]]:
        """Detect cube in an image using color segmentation and contour analysis"""
        try:
            # Convert to HSV and create blue mask
            hsv_image = cv2.cvtColor(image, cv2.COLOR_BGR2HSV)
            lower_blue = np.array([100, 50, 50])
            upper_blue = np.array([130, 255, 255])
            mask = cv2.inRange(hsv_image, lower_blue, upper_blue)
            
            # Find contours
            contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
            
            if contours:
                # Find the largest contour (likely the cube)
                largest_contour = max(contours, key=cv2.contourArea)
                
                # Approximate the contour to a polygon
                epsilon = 0.02 * cv2.arcLength(largest_contour, True)
                approx = cv2.approxPolyDP(largest_contour, epsilon, True)
                
                # If the polygon has 4 vertices, it's likely a cube face
                if len(approx) == 4:
                    # Return the vertices
                    return [tuple(point[0]) for point in approx]
            
            return None
            
        except Exception as e:
            print(f"Error detecting cube in image: {e}")
            return None