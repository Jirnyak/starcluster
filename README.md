<div align="center">

<img src="https://raw.githubusercontent.com/marko1olo/gigahrush/main/docs/banner_starcluster.jpg" width="100%" alt="Starcluster Banner"/>

# 🌌 STARCLUSTER — Real-Time Space Economy Sandbox

[![Language](https://img.shields.io/badge/Language-C%2B%2B23%20%2F%20SDL2-blue?style=for-the-badge&logo=cplusplus)]()
[![Simulation](https://img.shields.io/badge/Simulation-10%2C000%20Stars-gold?style=for-the-badge)]()
[![License](https://img.shields.io/badge/License-Open%20People's-brightgreen?style=for-the-badge)](LICENSE.md)

> **Sublight C++/SDL2 space economy sandbox inside one dense 10,000-star globular cluster — stale light-speed signals, local element markets, autonomous NPC factions.**

</div>

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
