import os
import numpy as np

PATH=os.path.dirname(os.path.realpath(__file__))

print(PATH[0:len(PATH)-5])

def sigmoid(argu):
        return (1+np.exp(-0.5)) / (1 + np.exp( - (argu-0.5)))

print(sigmoid(0))
print(sigmoid(0.66))
print(sigmoid(1))

def sigmoid2(argu):
        return (1) / (1 + np.exp( - argu))

print(sigmoid2(0))
print(sigmoid2(1))
print(sigmoid2(2))

