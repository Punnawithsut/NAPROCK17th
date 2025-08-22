# NAPROCK17th
NAPROCK17th

# GOALS
maximize pairs , minimize moves, <= 4.5 minutes

# DEV MANUAL
WHen somebody tring to implement a new algorithm for this problem please create a new branch and ask other devloper for checking.

Algorithm in branch main will always be the best algorithm

# IDEA
Nature of problem: Non-Convex optimization problem

Fitness for optimization (to maximize): F = Score - α * (moves) — ใช้ α เล็กๆ เวลา optimize แบบ budget-limited

Testing & benchmarking
Create instance set across sizes (6,12,16,20,24). Measure time-to-best, score and moves.

Interest Algorithm:
Large Neighborhood Search (LNS) + Greedy Repair: เก็บส่วนเหลือ
Memetic GA  + LNS: GA เเบบ mem 
Greedy Randomized Adaptive Search Procedure (GRASP): ตามชื่อ
Bidirection Search with intermediate stateas: 
