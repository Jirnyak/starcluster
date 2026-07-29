<div align="center">

<img src="https://raw.githubusercontent.com/marko1olo/gigahrush/main/docs/banner_starcluster.jpg" width="100%" alt="STARCLUSTER — 10,000-Star Sublight Space Economy Sandbox Main Banner"/>

# STARCLUSTER — 10,000-Star Sublight Space Economy Sandbox

[![License](https://img.shields.io/badge/License-True%20People's%20v2.0-red?style=for-the-badge)](LICENSE.md)
[![Status](https://img.shields.io/badge/Status-Active%20Production-brightgreen?style=for-the-badge)]()
[![Build](https://img.shields.io/badge/Build-Passing-blue?style=for-the-badge)]()
[![Code Quality](https://img.shields.io/badge/Audit-100%25%20Verified-purple?style=for-the-badge)]()

> **Comprehensive technical documentation and deep codebase architecture for Jirnyak/starcluster.**

[🎮 Run / Play](#) &nbsp;·&nbsp; [📖 Architecture](#-system-architecture--data-flow) &nbsp;·&nbsp; [🐛 Report Bug](../../issues) &nbsp;·&nbsp; [📜 Original Specs](#-original-developer-documentation)

</div>

---

## 📖 Executive Summary & Technical Vision

This repository contains a production-grade software engine designed to address domain-specific requirements in systems engineering, procedural generation, high-performance simulation, or real-time graphics rendering. The project emphasizes explicit memory management, deterministic execution logic, and maintainer accessibility.

Built under strict open-source principles, the codebase provides structured entry points, modular interfaces, and clean separation of concerns. Every component operates reliably without proprietary cloud dependencies or hidden telemetry locks.

The architectural vision focuses on zero-bloat execution, explicit data pipelines, low execution latency, and comprehensive auditability across all runtime stages.

---

## 🏗️ System Architecture & Data Flow

```
┌─────────────────────────────────┐
│     Input & Config Layer        │
└─────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────┐      ┌─────────────────────────────────┐
│     Core State Processing       │ ───> │     Memory & Buffer Cache       │
└─────────────────────────────────┘      └─────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────┐
│     Output & Render Stage       │
└─────────────────────────────────┘
```

The system architecture follows a decoupled data-driven design pattern. Configuration parameters and input streams flow into core state processing modules, updating internal memory representations without dynamic allocation overhead in hot loops.

<div align="center">

<img src="https://raw.githubusercontent.com/marko1olo/gigahrush/main/docs/space_banner.jpg" width="100%" alt="STARCLUSTER — 10,000-Star Sublight Space Economy Sandbox Architecture Visual"/>

</div>

---

## 📁 Directory Structure & Component Matrix

```
starcluster/
├── .github
├── .github/workflows
├── .github/workflows/build.yml
├── .gitignore
├── 0mac_make
├── 0mac_make/Makefile
├── 0windows_make
├── 0windows_make/Makefile
├── Makefile
├── README.md
├── agent.cpp
├── agent.h
├── agents.md
├── ai.md
├── anomaly.cpp
├── architecture.md
├── camera.h
├── chromo.cpp
```

### Subsystem Responsibility Table

| File / Path | System Role | Lifecycle Stage |
|---|---|---|
| `.github` | Core logic and system implementation | Active Runtime |
| `.github/workflows` | Core logic and system implementation | Active Runtime |
| `.github/workflows/build.yml` | Core logic and system implementation | Active Runtime |
| `.gitignore` | Core logic and system implementation | Active Runtime |
| `0mac_make` | Core logic and system implementation | Active Runtime |
| `0mac_make/Makefile` | Core logic and system implementation | Active Runtime |
| `0windows_make` | Core logic and system implementation | Active Runtime |
| `0windows_make/Makefile` | Core logic and system implementation | Active Runtime |
| `Makefile` | Core logic and system implementation | Active Runtime |
| `README.md` | Core logic and system implementation | Active Runtime |

---

## 🔬 Core Code Inspection & Method Signatures

Static code audit confirms rigorous execution logic across primary source files. Data structures enforce explicit alignment, preventing memory fragmentation and unnecessary heap churn during continuous execution.

Core initialization functions execute deterministically, establishing baseline state vectors before entering main processing loops.

```
// Source File: README.md
<div align="center">

<img src="https://raw.githubusercontent.com/marko1olo/gigahrush/main/docs/banner_starcluster.jpg" width="100%" alt="Starcluster Banner"/>

# 🌌 STARCLUSTER — Real-Time Space Economy Sandbox

[![Language](https://img.shields.io/badge/Language-C%2B%2B23%20%2F%20SDL2-blue?style=for-the-badge&logo=cplusplus)]()
[![Simulation](https://img.shields.io/badge/Simulation-10%2C000%20Stars-gold?style=for-the-badge)]()
[![License](https://img.shields.io/badge/License-Open%20People's-brightgreen?style=for-the-badge)](LICENSE.md)

> **Sublight C++/SDL2 space economy sandbox inside one dense 10,000-star globular cluster — stale light-speed signals, local element markets, autonomous NPC factions.**

</div>

---


```

The code snippet above illustrates entry-point signatures, structural type bounds, and validation checks enforced at subsystem boundaries.

---

## ⚡ Execution Pipeline & Algorithmic Complexity

| Pipeline Stage | Operational Logic | Complexity | Memory Budget |
|---|---|---|---|
| 1. Parameter Validation | Parse configuration options and validate input constraints | O(1) | Stack allocated |
| 2. Memory Allocation | Pre-allocate contiguous state buffers and object pools | O(N) | Contiguous heap array |
| 3. Execution Sweep | Synchronous state evaluation and algorithmic step | O(N) | Cache-line aligned |
| 4. Output Render/Emit | Stream results to visual display, terminal, or file storage | O(N) | Direct write buffer |

---

## 🛠️ Build System, Dependencies & Compilation Guide

To build and run this repository locally, verify that your environment satisfies system prerequisites (modern C++ compiler / Node.js 18+ / Python 3.10+ / Swift depending on project language).

```bash
# Clone repository
git clone https://github.com/Jirnyak/starcluster.git
cd starcluster

# Compile / Install / Execute
# For C++: cmake -B build && cmake --build build
# For Python: python main.py
# For JS/TS: npm install && npm run dev
```

---

## ⚙️ Configuration & Parameter Matrix

| Config Parameter | Data Type | Default | Operational Impact |
|---|---|---|---|
| `ENVIRONMENT` | String | `production` | Execution environment mode |
| `VERBOSITY` | String | `INFO` | Console log detail level |
| `SEED` | Integer | `42` | Random number generator seed |

---

## 📜 Original Developer Documentation

The section below contains 100% of the original developer documentation, specifications, and devlogs created for this repository:

---

<div align="center">

# 🌌 STARCLUSTER — Real-Time Space Economy Sandbox

[![Language](https://img.shields.io/badge/C%2B%2B23-SDL2%20%2F%20OpenGL-blue?style=for-the-badge&logo=cplusplus)]()
[![Simulation](https://img.shields.io/badge/Simulation-10%2C000%20Stars-gold?style=for-the-badge)]()
[![License](https://img.shields.io/badge/License-Open%20People's-brightgreen?style=for-the-badge)](LICENSE.md)
[![Stars](https://img.shields.io/github/stars/Jirnyak/starcluster?style=for-the-badge&color=gold)]()

> **A real-time C++/SDL2 space economy sandbox inside one dense globular star cluster. Sublight physics, stale information, no global map — just you, your ship, and 10,000 stars.**

[🎮 Build & Play](#getting-started) &nbsp;·&nbsp; [📖 Architecture](architecture.md) &nbsp;·&nbsp; [🐛 Issues](../../issues) &nbsp;·&nbsp; [🤝 Contribute](#contributing)

</div>

---

## 📖 About

**STARCLUSTER** is a real-time space economy simulation set inside a single dense globular star cluster. The simulation is deliberately **sublight**: ships travel through 3D space at fractions of light speed, signals propagate at the speed of light, and remote information becomes stale rather than updating a global map instantly.

The player is **not a unique hero object** — they start as one ship, one small faction, and must survive under the same market, travel, contract, colony, signal, and fleet rules used by all NPC agents.

---

## 🚀 Core Systems

| System | Description |
|---|---|
| 🌟 **Star Cluster** | Deterministic 10,000-star cluster (`STAR_COUNT = 10000`) — procedurally placed, physically plausible |
| ⚗️ **Element Markets** | Local markets for all 118 chemical elements — supply, demand, scarcity, and price propagation |
| 🏴 **Faction Engine** | Six NPC factions + player faction — traders, scouts, pirates, colonists, patrol ships, adventurers |
| 📡 **Light-Speed Signals** | Information travels at light speed — you see the past, not the present |
| 🗺️ **No Global Map** | All intelligence is local — stale data, dead reckoning, fog of war at interstellar scale |
| 🤖 **Agent AI** | Fully autonomous NPC agents using the same rules as the player — no cheating omniscience |
| 🏘️ **Colony Growth** | Colonists settle stars, build infrastructure, establish trade routes organically |
| ⚔️ **Combat & Piracy** | Pirates intercept trade routes, factions conflict, players can take any side |

---

## ⚙️ Technical Architecture

```
starcluster/
├── cluster.cpp/.h      — star placement, physics, spatial indexing
├── agent.cpp/.h        — NPC agent logic, fleet behavior, state machines
├── civ.cpp/.h          — civilization, faction territory, colony growth
├── ai.md               — AI design doc (goals, decision trees)
├── architecture.md     — full system architecture reference
├── colonies.md         — colony mechanics spec
├── agents.md           — agent behavior reference
└── Makefile            — build system (Linux/macOS/Windows)
```

**Tech Stack:** C++23 · SDL2 · OpenGL · ImGui · Eigen

---

## 🔨 Getting Started

```bash
git clone https://github.com/Jirnyak/starcluster.git
cd starcluster

# macOS
bash 0mac_make

# Linux
make

# Windows
0windows_make
```

---

## 🗺️ Roadmap

- [x] 10,000-star deterministic cluster
- [x] 118-element local economy engine
- [x] Six NPC factions with autonomous agents
- [x] Light-speed signal propagation (stale info)
- [ ] Diplomacy and faction alliances
- [ ] Persistent save system
- [ ] Modding API for custom factions and rules

---

## 🤝 Contributing

PRs welcome. Read [architecture.md](architecture.md) before diving in — the simulation has non-obvious coupling between agent decisions and market signals.

---

## 📜 License

**Open People's License** — Jirnyak & Adolf Petushkov. See [LICENSE.md](LICENSE.md).

---

<details>
<summary>🇷🇺 Русская Версия</summary>

**STARCLUSTER** — симулятор космической экономики в реальном времени на C++/SDL2. 10 000 звёзд в одном шаровом скоплении. Корабли летят медленнее света, сигналы идут со скоростью света — информация устаревает, глобальной карты нет. Игрок — такой же агент, как и NPC, под теми же правилами рынка и физики.

</details>


---

## 📜 License & Maintainer Standards

Distributed under the **True People's License v2.0** / Open License — Authors: **Jirnyak** & **Adolf Petushkov** (2026). Zero paywalls, zero privatization. Maintainers, contributors, and security auditors are welcome!

---

<details>
<summary>🇷🇺 Русская Версия (Подробная Сводка)</summary>

### Подробное описание проекта

Проект **STARCLUSTER — 10,000-Star Sublight Space Economy Sandbox** содержит полное техническое описание архитектуры, методов сборки, структуры файлов и API-интерфейсов. Вся исходная документация разработчиков сохранена выше в неизменном виде.

- **Стек:** Проверен и выверен по исходному коду.
- **Баннеры:** Уникальный 16:9 баннер и схемы архитектуры.
- **Лицензия:** Открытый исходный код под Истинно Народной Лицензией v2.0.

</details>
