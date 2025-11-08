import cv2
import numpy as np
from typing import List, Tuple, Optional

class ImageProcessor:
    def __init__(self):
        self.lower_blue = np.array([100, 50, 50])
        self.upper_blue = np.array([130, 255, 255])
    
    def load_image(self, image_path: str) -> np.ndarray:
        """Load image from file"""
        image = cv2.imread(image_path)
        if image is None:
            raise FileNotFoundError(f"Image not found: {image_path}")
        return image
    
    def convert_to_hsv(self, image: np.ndarray) -> np.ndarray:
        """Convert BGR image to HSV color space"""
        return cv2.cvtColor(image, cv2.COLOR_BGR2HSV)
    
    def create_color_mask(self, hsv_image: np.ndarray, 
                         lower_color: Optional[np.ndarray] = None,
                         upper_color: Optional[np.ndarray] = None) -> np.ndarray:
        """Create mask for specified color range"""
        if lower_color is None:
            lower_color = self.lower_blue
        if upper_color is None:
            upper_color = self.upper_blue
        
        return cv2.inRange(hsv_image, lower_color, upper_color)
    
    def find_contours(self, mask: np.ndarray, 
                     mode: int = cv2.RETR_EXTERNAL,
                     method: int = cv2.CHAIN_APPROX_SIMPLE) -> Tuple[List[np.ndarray], np.ndarray]:
        """Find contours in binary mask"""
        contours, hierarchy = cv2.findContours(mask, mode, method)
        return contours, hierarchy
    
    def find_convex_hull(self, contour: np.ndarray) -> np.ndarray:
        """Find convex hull of a contour"""
        return cv2.convexHull(contour)
    
    def image_to_point_cloud(self, image: np.ndarray) -> np.ndarray:
        """Convert image to 3D point cloud using intensity as Z-axis"""
        gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
        _, thresh = cv2.threshold(gray, 0, 255, 0)
        contours, _ = cv2.findContours(thresh, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        
        point_cloud = []
        for contour in contours:
            for point in contour:
                x, y = point[0]
                z = gray[y, x]
                point_cloud.append([x, y, z])
        
        return np.array(point_cloud)
    
    def draw_contours(self, image: np.ndarray, contours: List[np.ndarray], 
                     color: Tuple[int, int, int] = (0, 255, 0), 
                     thickness: int = 2) -> np.ndarray:
        """Draw contours on image"""
        return cv2.drawContours(image, contours, -1, color, thickness)
    
    def draw_points(self, image: np.ndarray, points: List[Tuple[int, int]], 
                   color: Tuple[int, int, int] = (0, 255, 0), 
                   radius: int = 5) -> np.ndarray:
        """Draw points on image"""
        result = image.copy()
        for point in points:
            cv2.circle(result, tuple(point), radius, color, -1)
        return result