import numpy as np
import open3d as o3d
from src.core.point_cloud_processor import PointCloudProcessor
from src.core.plane_detector import PlaneDetector
from src.utils.visualization import Visualizer

def plane_fitting_example():
    """Example of plane fitting and angle calculation"""
    print("Plane Fitting Example")
    
    # Create a sample point cloud with a tilted plane
    # Generate points for a plane tilted at 30 degrees
    x = np.linspace(-1, 1, 100)
    y = np.linspace(-1, 1, 100)
    xx, yy = np.meshgrid(x, y)
    
    # Create a plane tilted 30 degrees around the x-axis
    tilt_angle = np.radians(30)
    zz = yy * np.sin(tilt_angle)
    
    # Convert to point cloud
    points = np.vstack([xx.flatten(), yy.flatten(), zz.flatten()]).T
    
    # Add some noise
    points += np.random.normal(0, 0.01, points.shape)
    
    # Create Open3D point cloud
    pcd = o3d.geometry.PointCloud()
    pcd.points = o3d.utility.Vector3dVector(points)
    
    # Fit plane and calculate angle
    plane_detector = PlaneDetector()
    angle_deg, centroid, normal = plane_detector.detect_plane_angle(points)
    
    print(f"Detected plane angle: {angle_deg:.2f} degrees")
    print(f"Plane centroid: {centroid}")
    print(f"Plane normal: {normal}")
    
    # Create a visualization with the plane mesh
    plane_mesh = plane_detector.create_plane_mesh(centroid, normal, size=2.0)
    plane_mesh.paint_uniform_color([0.8, 0.8, 0.8])
    
    # Visualize
    Visualizer.draw_point_clouds([pcd, plane_mesh], "Plane Fitting Example")

if __name__ == "__main__":
    plane_fitting_example()