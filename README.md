
### Ludwig

A lattice Boltzmann code for complex fluids

[![Build Status](https://travis-ci.com/ludwig-cf/ludwig.svg?branch=feature-make)](https://travis-ci.com/ludwig-cf/ludwig)
[![CII Best Practices](https://bestpractices.coreinfrastructure.org/projects/1998/badge)](https://bestpractices.coreinfrastructure.org/projects/1998)


Ludwig is a parallel code for the simulation of complex fluids, which
include mixtures, colloidal suspensions, gels, and liquid crystals.
It takes its name from Ludwig Boltzmann, as it uses a lattice Boltzmann
method as a basis for numerical solution of the Navier Stokes equations
for hydrodynamics. It typically combines hydrodynamics with a coarse-grained
order parameter (or order parameters) to represent the "complex" part
in a free energy picture.

The code is written in standard ANSI C, and uses the Message Passing
Interface for distributed memory parallelism. Threaded parallelism is
also available via a lightweight abstraction layer ("Target Data Parallel"
or "TargetDP") which currently supports either OpenMP or CUDA (NVIDIA GPUs)
from a single source.

#### Installation

Copy a config file from the config directory to
the top level directory and make any changes required. E.g.,

```
$ cp config/unix-gcc-default.mk config.mk
$ make serial
$ make
$ make test
```
If a parallel build is wanted omit the serial step, for example,
```
$ cp config/unix-mpicc-default.mk config.mk
$ make
$ make test
```


Full details of the build process are available at
<a href = "https://ludwg.epcc.ed.ac.uk/">https://ludwig.epcc.ed.ac.uk/</a>.

#### Usage

Full documentation is available in the `docs` directory.

#### Contributing

If you would like to contribute, please consider a pull request.


#### Help

For bug reports, problems, and other issues, please open a new issue.


