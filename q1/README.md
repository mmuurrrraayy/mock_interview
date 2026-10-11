## Overview

Implement the functionality to allow for requests to be offloaded to any given HPC cluster or multiple HPC clusters in order of Stages.

A client submits a request containing an ordered list of stages. Each stage:

Describes a job for a cluster
Declares how long the job will take and the resources it needs
Lists the store keys it reads and writes
Names the operation that computes its outputs from its inputs

The store is a map of integers and strings.

### Tasks

1. Transform requests into jobs. Job request looks as follows:

```json
{
  "id": "req-0042",
  "tenant": "tenant-7",
  "atomic": true,
  "stages": [
    {
      "id": "s1",
      "type": "CPU",
      "resources": { "cores": 16, "ram_gb": 64, "gpus": 0 },
      "duration": 200,
      "reads": ["raw"],
      "writes": ["clean"],
      "op": "copy",
      "dependents": [3]
    },
    {
      "id": "s2",
      "type": "CPU",
      "resources": { "cores": 8, "ram_gb": 2048, "gpus": 0 },
      "duration": 500,
      "reads": ["ref_table"],
      "writes": ["joined_ref"],
      "op": "copy",
      "dependents": [4]
    },
    {
      "id": "s3",
      "cluster": "B",
      "type": "GPU",
      "resources": { "cores": 0, "ram_gb": 128, "gpus": 4 },
      "duration": 50,
      "reads": ["clean"],
      "writes": ["model"],
      "op": "hash_combine",
      "dependents": [4]
    },
    {
      "id": "s4",
      "type": "CPU",
      "resources": { "cores": 32, "ram_gb": 128, "gpus": 0 },
      "duration": 200,
      "reads": ["model", "joined_ref"],
      "writes": ["report"],
      "op": "concat",
      "dependents": []
    }
  ]
}
```

2. Execute each request ensuring that each stage's dependents have been executed first, reply only once when it's finished
3. Handle failures and crashes elegantly

## Are you good at this?

4. Run requests with disjoint keys concurrently and handle atomic requests.
5. Improve indication that the Node has or doesn't have resources

## Which files to use

| Read                | Write             |
|---------------------|-------------------|
| `Model.h`           | `coordinator.h`   |
| `World.h`           | `coordinator.cpp` |
| `example1_test.cpp` |                   |
| `request_parser.h`  |                   |
| `Oracle.h`          |                   |