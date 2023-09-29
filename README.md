# Backdoor Tree Search using Metaheuristic Minimization

This repository is a fork of [minisat](https://github.com/master-keying/minisat), a SAT solver designed for efficiently solving boolean satisfiability problems. In this fork, the code has been adapted to perform fast search for tree-structured backdoors using a metaheuristic minimization algorithm. This technique is detailed in the paper "Probabilistic Generalization of Backdoor Trees with Application to SAT" by Semenov, A. A.; Chivilikhin, D.; Kochemazov, S.; and Dzhiblavi, I. The paper was published in AAAI.

## Installation

To build the code, follow these steps:

1. Clone this repository or download the source code.
2. Open a terminal and navigate to the repository's root directory.
3. Run the following commands:

   ```bash
   mkdir build && cd build
   cmake ..
   make
## Usage
After building the code, you can use it to search for backdoors with the specified parameters. Here is an example command:

   ```bash
   build/minisat original.cnf -ea-seed=42 -ea-instance-size=10 -ea-num-runs=1000 -ea-num-iters=1000 
   ```

Replace `original.cnf` with the path to your CNF file.

## Parameters
1. -ea-instance-size: Size of the backdoor.
2. -ea-num-runs: Number of backdoors to search for.
3. -ea-num-iters: Number of iterations for each backdoor.
4. -ea-seed random seed for the algorithm 

The results of the backdoor search will be saved in the `backdoor.txt` file.


