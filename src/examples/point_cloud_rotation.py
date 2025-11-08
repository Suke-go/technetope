import numpy as np
import open3d as o3d
from src.utils.transformations import Transformations
from src.utils.visualization import Visualizer

def point_cloud_rotation_example():
    """Example of point cloud rotation and angle calculation"""
    print("Point Cloud Rotation Example")
    
    # Create a sample point cloud (a simple cube)
    points = np.array([
        [0, 0, 0], [1, 0, 0], [0, 1, 0], [0, 0, 1],
        [1, 1, 0], [1, 0, 1], [0, 1, 1], [1, 1, 1]
    ])
    
    # Center the cube
    centroid = np.mean(points, axis=0)
    centered_points = points - centroid
    
    # Apply rotation (45° around X, 30° around Y)
    rotation_matrix = Transformations.create_rotation_matrix(
        np.radians(45), np.radians(30))
    rotated_points = Transformations.apply_rotation(centered_points, rotation_matrix)
    
    # Calculate rotation angles from the rotated points
    calculated_x, calculated_y = Transformations.calculate_rotation_angles(rotated_points)
    
    print(f"Applied rotation: 45° around X, 30° around Y")
    print(f"Calculated rotation: {np.degrees(calculated_x):.2f}° around X, "
          f"{np.degrees(calculated_y):.2f}° around Y")
    
    # Create point clouds for visualization
    original_pc = o3d.geometry.PointCloud()
    original_pc.points = o3d.utility.Vector3dVector(points)
    original_pc.paint_uniform_color([1, 0, 0])  # Red
    
    rotated_pc = o3d.geometry.PointCloud()
    rotated_pc.points = o3d.utility.Vector3dVector(rotated_points + centroid)
    rotated_pc.paint_uniform_color([0, 1, 0])  # Green
    
    # Visualize
    Visualizer.draw_point_clouds([original_pc, rotated_pc], "Point Cloud Rotation")

if __name__ == "__main__":
    point_cloud_rotation_example()