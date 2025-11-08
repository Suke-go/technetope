import unittest
import numpy as np
import open3d as o3d
from src.core.point_cloud_processor import PointCloudProcessor
from src.core.plane_detector import PlaneDetector

class TestPointCloud(unittest.TestCase):
    def setUp(self):
        self.processor = PointCloudProcessor()
        self.plane_detector = PlaneDetector()
        
        # Create a simple point cloud for testing
        self.sample_points = np.array([
            [0, 0, 0], [1, 0, 0], [0, 1, 0], [0, 0, 1],
            [1, 1, 0], [1, 0, 1], [0, 1, 1], [1, 1, 1]
        ])
        
        self.sample_pcd = o3d.geometry.PointCloud()
        self.sample_pcd.points = o3d.utility.Vector3dVector(self.sample_points)
    
    def test_downsample_point_cloud(self):
        downsampled = self.processor.downsample_point_cloud(self.sample_pcd, 0.5)
        self.assertLessEqual(len(downsampled.points), len(self.sample_pcd.points))
    
    def test_estimate_normals(self):
        pcd_with_normals = self.processor.estimate_normals(self.sample_pcd)
        self.assertTrue(pcd_with_normals.has_normals())
    
    def test_segment_plane(self):
        # Create a planar point cloud
        x = np.linspace(-1, 1, 10)
        y = np.linspace(-1, 1, 10)
        xx, yy = np.meshgrid(x, y)
        zz = np.zeros_like(xx)
        
        points = np.vstack([xx.flatten(), yy.flatten(), zz.flatten()]).T
        planar_pcd = o3d.geometry.PointCloud()
        planar_pcd.points = o3d.utility.Vector3dVector(points)
        
        plane_model, inliers = self.processor.segment_plane(planar_pcd)
        self.assertEqual(len(plane_model), 4)  # Plane equation: ax+by+cz+d=0
        self.assertGreater(len(inliers), 0)    # Should find inliers
    
    def test_fit_plane_to_points(self):
        # Create a planar point cloud
        x = np.linspace(-1, 1, 10)
        y = np.linspace(-1, 1, 10)
        xx, yy = np.meshgrid(x, y)
        zz = np.zeros_like(xx)
        
        points = np.vstack([xx.flatten(), yy.flatten(), zz.flatten()]).T
        
        centroid, normal = self.plane_detector.fit_plane_to_points(points)
        
        # Normal should be close to [0, 0, 1] (vertical)
        self.assertTrue(np.allclose(normal, [0, 0, 1], atol=0.1))
        
        # Centroid should be close to [0, 0, 0]
        self.assertTrue(np.allclose(centroid, [0, 0, 0], atol=0.1))

if __name__ == "__main__":
    unittest.main()