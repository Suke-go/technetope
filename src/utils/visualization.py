import open3d as o3d
import cv2
import numpy as np
from typing import List, Optional

class Visualizer:
    @staticmethod
    def draw_point_clouds(point_clouds: List[o3d.geometry.PointCloud], 
                         window_name: str = "Open3D Visualization"):
        """Visualize multiple point clouds"""
        o3d.visualization.draw_geometries(point_clouds, window_name=window_name)
    
    @staticmethod
    def create_visualizer() -> o3d.visualization.Visualizer:
        """Create and configure Open3D visualizer"""
        vis = o3d.visualization.Visualizer()
        vis.create_window()
        return vis
    
    @staticmethod
    def update_visualizer(vis: o3d.visualization.Visualizer, 
                         geometries: List[o3d.geometry.Geometry]):
        """Update visualizer with new geometries"""
        vis.clear_geometries()
        for geometry in geometries:
            vis.add_geometry(geometry)
        vis.poll_events()
        vis.update_renderer()
    
    @staticmethod
    def show_images(images: List[np.ndarray], titles: List[str]):
        """Display multiple OpenCV images"""
        for i, (image, title) in enumerate(zip(images, titles)):
            cv2.imshow(title, image)
        
        cv2.waitKey(0)
        cv2.destroyAllWindows()
    
    @staticmethod
    def create_cube_vertices(center: np.ndarray, dimensions: np.ndarray) -> np.ndarray:
        """Create 3D cube vertices from center and dimensions"""
        half_dims = dimensions / 2
        vertices = np.array([
            center + [-half_dims[0], -half_dims[1], -half_dims[2]],
            center + [-half_dims[0], -half_dims[1],  half_dims[2]],
            center + [-half_dims[0],  half_dims[1], -half_dims[2]],
            center + [-half_dims[0],  half_dims[1],  half_dims[2]],
            center + [ half_dims[0], -half_dims[1], -half_dims[2]],
            center + [ half_dims[0], -half_dims[1],  half_dims[2]],
            center + [ half_dims[0],  half_dims[1], -half_dims[2]],
            center + [ half_dims[0],  half_dims[1],  half_dims[2]]
        ])
        return vertices