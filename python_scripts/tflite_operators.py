"""
Print the operators in a TFLite model.

Usage:
    python tflite_operators.py <tflite_path>
"""

import tensorflow as tf
import argparse

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("tflite", type=str, help="Path to the model file")
    args = parser.parse_args()

    tflite_path = args.tflite

    interp = tf.lite.Interpreter(model_path=tflite_path)
    interp.allocate_tensors()

    op_details = interp._get_ops_details()  # ok for debugging
    for i, op in enumerate(op_details):
        print(i, op["op_name"])
