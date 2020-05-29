import os
import shutil

for filename in os.listdir(os.path.dirname(os.path.abspath(__file__))):
    if '_' in filename:
        new_filename = filename.replace('_', '-')
        shutil.move(filename, new_filename)