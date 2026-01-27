#!/usr/bin/env python3
"""
Extract a CIFAR-10 test image and convert it to a C header file.
This script downloads CIFAR-10 test batch and extracts the first image.
"""

import sys
import os
import urllib.request
import pickle
import numpy as np

def download_cifar10_test():
    """Download CIFAR-10 test batch if not present."""
    test_file = 'cifar-10-batches-py/test_batch'
    if not os.path.exists(test_file):
        print("Downloading CIFAR-10 dataset...")
        url = 'https://www.cs.toronto.edu/~kriz/cifar-10-python.tar.gz'
        tar_file = 'cifar-10-python.tar.gz'
        
        print(f"Downloading from {url}...")
        urllib.request.urlretrieve(url, tar_file)
        
        import tarfile
        print("Extracting...")
        with tarfile.open(tar_file, 'r:gz') as tar:
            tar.extractall()
        
        os.remove(tar_file)
        print("Download complete!")
    
    return test_file

def extract_image(image_index=0):
    """Extract a CIFAR-10 test image."""
    test_file = download_cifar10_test()
    
    # Load test batch
    with open(test_file, 'rb') as f:
        batch = pickle.load(f, encoding='bytes')
    
    # Get image and label
    images = batch[b'data']
    labels = batch[b'labels']
    
    if image_index >= len(images):
        image_index = 0
        print(f"Warning: image_index too large, using 0")
    
    # Extract image (3072 bytes: 1024 R + 1024 G + 1024 B)
    image_data = images[image_index]
    label = labels[image_index]
    
    # CIFAR-10 labels
    label_names = ['airplane', 'automobile', 'bird', 'cat', 'deer', 
                   'dog', 'frog', 'horse', 'ship', 'truck']
    
    print(f"Extracted image {image_index}:")
    print(f"  Label: {label} ({label_names[label]})")
    print(f"  Image shape: {image_data.shape}")
    print(f"  Pixel range: [{image_data.min()}, {image_data.max()}]")
    
    return image_data, label, label_names[label]

def generate_c_header(image_data, label_name, output_file='src/cifar10_test_image.h'):
    """Generate C header file with image data."""
    # Convert to list for C array
    image_array = image_data.tolist()
    
    # Generate header file
    header_content = f"""#ifndef CIFAR10_TEST_IMAGE_H_
#define CIFAR10_TEST_IMAGE_H_

// CIFAR-10 Test Image
// Label: {label_name}
// Size: 32x32x3 = 3072 bytes
// Format: RGB, uint8 values [0-255]

#include <stdint.h>

const uint8_t cifar10_test_image[3072] = {{
"""
    
    # Write array data (16 values per line for readability)
    for i in range(0, len(image_array), 16):
        line_values = image_array[i:i+16]
        line = ', '.join(f'{val:3d}' for val in line_values)
        header_content += f"    {line}"
        if i + 16 < len(image_array):
            header_content += ","
        header_content += "\n"
    
    header_content += """};

#endif // CIFAR10_TEST_IMAGE_H_
"""
    
    # Write to file
    with open(output_file, 'w') as f:
        f.write(header_content)
    
    print(f"\nGenerated: {output_file}")
    print(f"  Array size: {len(image_array)} bytes")
    print(f"  Use in code: #include \"cifar10_test_image.h\"")

if __name__ == "__main__":
    image_index = 0
    if len(sys.argv) > 1:
        image_index = int(sys.argv[1])
    
    print("Extracting CIFAR-10 test image...")
    image_data, label, label_name = extract_image(image_index)
    
    output_file = 'src/cifar10_test_image.h'
    if len(sys.argv) > 2:
        output_file = sys.argv[2]
    
    generate_c_header(image_data, label_name, output_file)
    print(f"\nDone! Image {image_index} ({label_name}) saved to {output_file}")
