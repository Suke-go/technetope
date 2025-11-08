import numpy as np
import open3d as o3d
from scipy.spatial.transform import Rotation as R
from typing import Tuple

class PlaneDetector:
    @staticmethod
    def fit_plane_to_points(points: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
        """Fit a plane to 3D points using SVD"""
        centroid = np.mean(points, axis=0)
        centered_points = points - centroid
        _, _, Vt = np.linalg.svd(centered_points)
        normal = Vt[-1]
        return centroid, normal
    
    @staticmethod
    def angle_between_vectors(v1: np.ndarray, v2: np.ndarray) -> float:
        """Calculate angle between two vectors in radians"""
        cos_angle = np.dot(v1, v2) / (np.linalg.norm(v1) * np.linalg.norm(v2))
        return np.arccos(np.clip(cos_angle, -1, 1))
    
    @staticmethod
    def create_plane_mesh(centroid: np.ndarray, normal: np.ndarray, 
                         size: float = 1.0, resolution: int = 10) -> o3d.geometry.TriangleMesh:
        """Create a plane mesh aligned with the detected normal"""
        # Create coordinate frame and align it with the normal
        plane = o3d.geometry.TriangleMesh.create_coordinate_frame(size=size)
        
        # Align the plane with the detected normal
        rotation, _ = R.align_vectors([[0, 0, 1]], [normal])
        plane.rotate(rotation.as_matrix(), center=[0, 0, 0])
        
        # Move to centroid
        plane.translate(centroid)
        
        return plane
    
    def detect_plane_angle(self, points: np.ndarray, 
                          reference_vector: np.ndarray = np.array([0, 0, 1])) -> Tuple[float, float]:
        """Detect plane and return angle relative to reference vector"""
        centroid, normal = self.fit_plane_to_points(points)
        angle_rad = self.angle_between_vectors(normal, reference_vector)
        angle_deg = np.degrees(angle_rad)
        
        return angle_deg, centroid, normal