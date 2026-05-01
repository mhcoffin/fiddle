# Metronome Clicks from Dorico

Summary of the tables below:

* Simple meters (4/4, 2/4, 3/4, 3/8) have two kinds of beats: strong and medium. The strong beats are on beat 0, and are note 83 with velocity 64. The medium beats are on the remaining beats, and are note 79 with velocity 56.

* Compound meters (6/8, 12/8) have three kinds of beats: strong, medium, and weak. The strong beats are on beat 0, and are note 83 with velocity 64. The medium beats are on beats 3 and 6 (although other compound meters would have different beat indices), and are note 79 with velocity 56. The weak beats are on the remaining beats, and are note 71 with velocity 48.

* Irregular meters (5/4, 7/8, etc.) have different patterns of beats, but the initial beat is always a strong beat (note 83 with velocity 64). 

So we can detect bar lines by finding the strong beats (note 83 with velocity 64). 



## 4/4

| Beat | Note | Velocity |
|------|------|----------|
| 0    | 83   | 64       |
| 1    | 79   | 56       |
| 2    | 79   | 56       |
| 3    | 79   | 56       |

## 2/4

| Beat | Note | Velocity |
|------|------|----------|
| 0    | 83   | 64       |
| 1    | 79   | 56       |

## 3/4

| Beat | Note | Velocity |
|------|------|----------|
| 0    | 83   | 64       |
| 1    | 79   | 56       |
| 2    | 79   | 56       |

## 3/8  

| Beat | Note | Velocity |
|------|------|----------|
| 0    | 83   | 64       |
| 1    | 79   | 56       |
| 2    | 79   | 56       |

## 6/8

| Beat | Note | Velocity |
|------|------|----------|
| 0    | 83   | 64       |
| 1    | 71   | 48       |
| 2    | 71   | 48       |
| 3    | 79   | 56       |
| 4    | 71   | 48       |
| 5    | 71   | 48       |

## 12/8

| Beat | Note | Velocity |
|------|------|----------|
| 0    | 83   | 64       |
| 1    | 71   | 48       |
| 2    | 71   | 48       |
| 3    | 79   | 56       |
| 4    | 71   | 48       |
| 5    | 71   | 48       |
| 6    | 79   | 56       |
| 7    | 71   | 48       |
| 8    | 71   | 48       |
| 9    | 79   | 56       |
| 10   | 71   | 48       |
| 11   | 71   | 48       |

