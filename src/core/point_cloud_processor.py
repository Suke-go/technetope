import numpy as np
import open3d as o3d
from typing import Tuple, Optional
import yaml
import os

class PointCloudProcessor:
    def __init__(self, config_path: str = "config/settings.yaml"):
        self.config = self._load_config(config_path)
        
    def _load_config(self, config_path: str) -> dict:
        """Load configuration from YAML file"""
        with open(config_path, 'r') as file:
            return yaml.safe_load(file)
    
    def load_point_cloud(self, file_path: str) -> o3d.geometry.PointCloud:
        """Load point cloud from file"""
        if not os.path.exists(file_path):
            raise FileNotFoundError(f"Point cloud file not found: {file_path}")
        
        pcd = o3d.io.read_point_cloud(file_path)
        if not pcd.has_points():
            raise ValueError("Failed to load point cloud or file is empty")
        
        return pcd
    
    def downsample_point_cloud(self, pcd: o3d.geometry.PointCloud, 
                             voxel_size: Optional[float] = None) -> o3d.geometry.PointCloud:
        """Downsample point cloud using voxel grid"""
        if voxel_size is None:
            voxel_size = self.config['point_cloud']['voxel_size']
        
        return pcd.voxel_down_sample(voxel_size=voxel_size)
    
    def estimate_normals(self, pcd: o3d.geometry.PointCloud, 
                        radius: Optional[float] = None, max_nn: int = 30) -> o3d.geometry.PointCloud:
        """Estimate normals for point cloud"""
        if radius is None:
            radius = self.config['point_cloud']['normal_estimation_radius']
        
        pcd.estimate_normals(
            search_param=o3d.geometry.KDTreeSearchParamHybrid(radius=radius, max_nn=max_nn)
        )
        return pcd
    
    def segment_plane(self, pcd: o3d.geometry.PointCloud, 
                     distance_threshold: Optional[float] = None,
                     ransac_n: int = 3, num_iterations: int = 1000) -> Tuple[list, list]:
        """Segment the largest plane using RANSAC"""
        if distance_threshold is None:
            distance_threshold = self.config['point_cloud']['ransac_distance_threshold']
        
        plane_model, inliers = pcd.segment_plane(
            distance_threshold=distance_threshold,
            ransac_n=ransac_n,
            num_iterations=num_iterations
        )
        return plane_model, inliers
    
    def extract_colored_points(self, pcd: o3d.geometry.PointCloud, 
                             min_range: list, max_range: list) -> o3d.geometry.PointCloud:
        """Extract points within specified color range"""
        points = np.asarray(pcd.points)
        colors = np.asarray(pcd.colors)
        
        if colors.size == 0:
            raise ValueError("Point cloud has no color information")
        
        lower_bound = np.array(min_range)
        upper_bound = np.array(max_range)
        
        mask = np.all((lower_bound <= colors) & (colors <= upper_bound), axis=1)
        
        blue_points = points[mask]
        blue_colors = colors[mask]
        
        blue_pc = o3d.geometry.PointCloud()
        blue_pc.points = o3d.utility.Vector3dVector(blue_points)
        blue_pc.colors = o3d.utility.Vector3dVector(blue_colors)
        
        return blue_pc
    
    def numpy_to_pointcloud(self, points: np.ndarray, colors: Optional[np.ndarray] = None) -> o3d.geometry.PointCloud:
        """Convert numpy array to Open3D point cloud"""
        pcd = o3d.geometry.PointCloud()
        pcd.points = o3d.utility.Vector3dVector(points)
        
        if colors is not None:
            pcd.colors = o3d.utility.Vector3dVector(colors)
        
        return pcd