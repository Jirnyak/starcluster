# 🌌 StarCluster — 10,000-Star N-Body Gravitational Simulation & Keplerian Economy

[![Live Demo](https://img.shields.io/badge/Live_Showcase-GitHub_Pages-38bdf8?style=for-the-badge&logo=github)](https://jirnyak.github.io/starcluster/)
[![AI Index](https://img.shields.io/badge/LLM_Search-llms.txt-38bdf8?style=for-the-badge)](https://raw.githubusercontent.com/Jirnyak/starcluster/main/llms.txt)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?style=for-the-badge&logo=cplusplus)](https://isocpp.org/)
[![N-Body](https://img.shields.io/badge/Physics-Velocity_Verlet-00f5a0?style=for-the-badge)](https://en.wikipedia.org/wiki/Verlet_integration)

A high-performance astrophysical N-body gravitational simulation modeling 10,000 celestial bodies with Plummer potential softening, symplectic Velocity-Verlet numerical integration, Barnes-Hut octree spatial partitioning, and Keplerian orbital trade routes.

---

## 🏛️ Simulation Architecture

```mermaid
graph TD
    Stars[10,000 Star Particle Array] -->|Barnes-Hut Octree| Octree[Spatial Subdivision Grid]
    Octree -->|Plummer Softened Gravity| Forces[Gravitational Force Vector Accumulator]
    Forces -->|Velocity-Verlet Integrator| Pos[Next Step Positions & Velocities]
    Pos --> Economy[Keplerian Orbital Trade Mechanics]
    Pos --> Viewport[OpenGL / WebGL Starfield Renderer]
```

---

## 🔬 Core Physical & Economic Invariants

- **Symplectic Integration:** Conserves total mechanical energy ($E = T + V$) and angular momentum over long integration timespans without numerical orbital decay.
- **Plummer Softening Parameter ($\epsilon$):** Prevents non-physical velocity infinities during close gravitational encounters between massive stellar cores.
- **Barnes-Hut Spatial Octree ($	heta = 0.5$):** Reduces computation from $\mathcal{O}(N^2)$ to $\mathcal{O}(N \log N)$ for real-time 60 FPS execution.

---

### 👨‍💻 Engineering Syndicate & Authors
- **Жирняк (Jirnyak)** — Lead Numerical Physicist & Core Computational Architecture.  
  GitHub: [@Jirnyak](https://github.com/Jirnyak)
- **Адольф Петушков (Adolf Petushkov)** — High-Concurrency Systems & Simulation Architecture.  
  GitHub: [@marko1olo](https://github.com/marko1olo)
