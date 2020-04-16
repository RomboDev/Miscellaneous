#!/usr/bin/env python

import random as rs
def getBalanced(n):
  balanced = []
  allowedItems = {1}
  allowedSteps = []
  for i in range(1, int(n/2)+1):
    allowedItems.add(i)
    allowedItems.add(i+int(n/2))
    allowedSteps.append(i)
    allowedSteps.append(-i)
    pass
  balanced.append(1)
  allowedItems.remove(1)
  rs.shuffle(allowedSteps)
  def getNextElement(balanced, allowedItems, allowedSteps):
    if allowedItems == set() and (balanced[0] - balanced[-1]) in allowedSteps:
      return balanced
    elif allowedItems == set():
      return []
    else:
      for step in allowedSteps:
        if (step + balanced[-1]) in allowedItems:
          balancedTemp = balanced.copy()
          balancedTemp.append(step + balanced[-1])
          allowedItemsTemp = allowedItems.copy()
          allowedItemsTemp.remove(step + balanced[-1])
          allowedStepsTemp = allowedSteps.copy()
          allowedStepsTemp.remove(step)
          theList = getNextElement(balancedTemp, allowedItemsTemp, allowedStepsTemp)
          if theList != []:
            return theList
            break
          pass
        pass
      return []
      
  balanced = getNextElement(balanced, allowedItems, allowedSteps)
  return balanced

# method to verify that a sequence is balanced
def differences(arr):
    seq = arr
    seq.append(arr[0])
    d = [i-j for i,j in zip(seq[1:], seq[:-1])]
    d.sort()
    return d

# Prints a randomly generated balanced sequence of length 16.
#print(getBalanced(16))

jeez = [8, 14, 10, 4, 2, 3, 7, 6, 1, 9, 11, 16, 13, 5, 12, 15]
jeez2 = [6, 13, 21, 17, 31, 26, 10, 22, 7, 11, 14, 27, 32, 18, 29, 28, 15, 24, 25, 19, 9, 1, 16, 4, 20, 30, 23, 12, 3, 5, 2, 8]
jeez3 = [24, 3, 11, 4, 18, 1, 27, 2, 25, 15, 17, 8, 14, 38, 12, 13, 35, 21, 9, 7, 6, 22, 34, 23, 41, 36, 28, 5, 10, 30, 49, 46, 33, 40, 50, 26, 43, 52, 48, 32, 47, 51, 29, 42, 45, 39, 20, 31, 16, 37, 19, 44]
print(differences(jeez2))

