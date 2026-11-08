

import sys
from collections import defaultdict, Counter

def main():

    problem2()



def problem1():
    n = int(sys.stdin.readline())
    vals = list(map(int, sys.stdin.readline().split()))

    first = None
    second = None
    for i in vals:
        if first is None:
            first = i
        elif i > first:
            second = first
            first = i
        elif i < first and (second is not None) and i > second:
            second = i
        elif i < first and second is None:
            second = i
        
            
    if second is not None:
        print(second)
    else:
        print(-1)



def problem2():
    s = sys.stdin.readline().strip()
    t = sys.stdin.readline().strip()


    c1 = Counter(s)
    c2 = Counter(t)

    if (c1 == c2):
        print("YES")
    else:
        print("NO")

    d1 = defaultdict(int)
    d2 = defaultdict(int)

    for i in s:
        d1[i] += 1
    for i in t:
        d2[i] += 1

    if(d1 == d2):
        print("YES")
    else:
        print("NO")


def problem3():
    n = int(sys.stdin.readline())
    arr = list(map(int, sys.stdin.readline().split()))
    target = int(sys.stdin.readline())

    mappy = {}

    for i in range(n):
        val = arr[i]
        inverse = target - val
        if inverse in mappy:
            print(i, mappy[inverse])
            break
        mappy[val] = i


def problem4():
    n = int(sys.stdin.readline())
    words = sys.stdin.readline().split()

    groups = defaultdict(list)

    for word in words:
        k = "".join(sorted(word))
        groups[k].append(word)

    for group in groups.values():
        print(" ".join(group))

def problem5():
    n = int(sys.stdin.readline())
    nums = list(map(int, sys.stdin.readline().split()))
    target = int(sys.stdin.readline())

    left = 0 
    right = n-1

    while(left < right):
        total = nums[left] = nums[right]
        if(total == target):
            print(left, right)
            return
        elif total > target:
            right -= 1
        else:
            left += 1

    print(-1)


def problem6():
    s = sys.stdin.readline()

    inWindow = set()
    left = 0
    best = 0
    right = 0

    while(right < len(s)):
        while(s[right] in inWindow):
            inWindow.remove(s[left])
            left+= 1
        inWindow.add(s[right]) 
        right +=1
        best = max(best, len(inWindow))

    print(best)


def problem7():
    n, k = map(int, sys.stdin.readline().split())
    arr = list(map(int, sys.stdin.readline().split()))


    left = 0
    right = k

    sum = 0
    for i in range(k):
        sum += arr[i]
    best = sum 

    while(right < len(arr)):
        sum -= arr[left]
        sum += arr[right]
        best = max(best, sum)
        left += 1
        right += 1


    print(best)



def problem10():
    n = int(sys.stdin.readline())

    mod = 10**9 + 7

    dptab = [0] * (n+1)
    dptab[0] = 1
    dptab[1] = 1

    for i in range(2, n+1):
        dptab[i] = (dptab[i-1] + dptab[i-2])%mod

    print(dptab[n])




import bisect

def sortedSums(arr):

    def helper(arr):
        sum = 0
        for i in range(len(arr)):
            sum += arr[i]**(i+1)
        return sum

    cur = []
    total = 0
    for i in arr:
        bisect.insort(cur, i)
        total = (total + helper(cur) )% (10**9 + 7)
    return total




def matrixRotation(matrix, angle):
    rotations = (angle//90) % 4

    def rotate90(matrix):
        matrix = list(row for row in zip(*matrix[::-1]))
        return matrix

    for _ in range(rotations):
        matrix = rotate90(matrix)

    return matrix



def rotate90(matrix):
    matrix = list(row for row in zip(*matrix[::-1]))


def strictlyIncreasing(arr):
    first = float('inf')
    second = float('inf')

    for i in arr:
        if arr[i] <= first:
            first = i
        elif arr[i] <= second:
            second = i
        else:
            return True
    return False