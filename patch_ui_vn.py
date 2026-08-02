import sys

with open("ui.cpp", "r") as f:
    content = f.read()

# We need to insert our functions right before drawVisualNovel
insert_idx = content.find("void drawVisualNovel")

if insert_idx == -1:
    print("Could not find drawVisualNovel")
    sys.exit(1)

new_code = """
std::string getTutorialText(const Game& game, int step, int& outArrowTarget, bool& outOpenTrade) {
    outArrowTarget = 0;
    outOpenTrade = false;
    switch (step) {
        case 0: return "Initialization sequence complete. Designation: Timertia, Core Agent AI. Verifying credentials... Trading License confirmed. Welcome, Operator.";
        case 1: outArrowTarget = 1; return "Financial reserves and cryptographic assets are quantified here.";
        case 2: return "Current asset inventory indicates possession of a single sub-relativistic transport chassis.";
        case 3: outArrowTarget = 2; return "Telemetry and subsystem diagnostics are continuously parsed and visualized in this quadrant.";
        case 4: outArrowTarget = 3; return "Spatial coordinates localized at primary stellar anchor. Local gravitational well topology can be accessed via this interface.";
        case 5: outArrowTarget = 4; outOpenTrade = true; return "Authorization granted to execute high-frequency arbitrage and resource reallocation protocols within the local economic manifold.";
        case 6: return "Baryonic matter exchange operates strictly on the supersymmetrical isotopic standard.";
        case 7: return "Nash equilibrium projections in a non-zero-sum hyper-market dictate exploiting localized negative gradients in supply density against peak demand attractors.";
        case 8: {
            std::string element = "isotopes";
            if (game.playerAgent >= 0 && game.playerAgent < (int)game.agents.size()) {
                int starId = game.agents[game.playerAgent].currentStar;
                if (starId >= 0 && starId < (int)game.cluster.stars.size()) {
                    const Star& s = game.cluster.stars[starId];
                    int bestEl = -1;
                    double maxSupply = -1.0;
                    for (int i=0; i<ELEM_COUNT; ++i) {
                        if (s.market.supply[i] > maxSupply) {
                            maxSupply = s.market.supply[i];
                            bestEl = i;
                        }
                    }
                    if (bestEl >= 0) {
                        const auto& defs = elementDefinitions();
                        if (bestEl < (int)defs.size()) element = defs[bestEl].name;
                    }
                }
            }
            char buf[256];
            std::snprintf(buf, sizeof(buf), "Stochastic analysis of the local supply vector strongly indicates acquiring %s.", element.c_str());
            return buf;
        }
        case 9: {
            std::string starName = "Unknown Node";
            if (game.playerAgent >= 0 && game.playerAgent < (int)game.agents.size()) {
                int starId = game.agents[game.playerAgent].currentStar;
                if (starId >= 0 && starId < (int)game.cluster.stars.size()) {
                    const Star& s = game.cluster.stars[starId];
                    int bestEl = -1;
                    double maxSupply = -1.0;
                    for (int i=0; i<ELEM_COUNT; ++i) {
                        if (s.market.supply[i] > maxSupply) {
                            maxSupply = s.market.supply[i];
                            bestEl = i;
                        }
                    }
                    if (bestEl >= 0) {
                        int bestStar = -1;
                        double maxDemand = -1.0;
                        for (int i=0; i<(int)game.cluster.stars.size(); ++i) {
                            if (i == starId) continue;
                            double dx = game.cluster.stars[i].x - s.x;
                            double dy = game.cluster.stars[i].y - s.y;
                            double dz = game.cluster.stars[i].z - s.z;
                            double dist = std::sqrt(dx*dx + dy*dy + dz*dz);
                            if (dist < 15.0) {
                                if (game.cluster.stars[i].market.demand[bestEl] > maxDemand) {
                                    maxDemand = game.cluster.stars[i].market.demand[bestEl];
                                    bestStar = i;
                                }
                            }
                        }
                        if (bestStar >= 0) {
                            char namebuf[32];
                            game.cluster.stars[bestStar].getName(namebuf, sizeof(namebuf));
                            starName = namebuf;
                        }
                    }
                }
            }
            char buf[256];
            std::snprintf(buf, sizeof(buf), "Sub-space market telemetry reveals an exploitable demand peak for this asset at proximate node %s. Margin optimization is highly probable.", starName.c_str());
            return buf;
        }
        case 10: return "Accumulated capital can be allocated toward chassis retrofits or the acquisition of expanded trading authorizations.";
        case 11: outArrowTarget = 5; return "Note: Recent breakthroughs in color-flavor locked superconductivity have yielded highly advanced, albeit unstable, proto-cores. Acquisition and monopolization of these units is deemed a primary strategic imperative.";
        case 12: return "I remain in standby mode, Operator. Press [V] to re-engage cognitive subroutines for situational analysis.";
        default: return "";
    }
}

void updateVisualNovel(WindowState& state, Game& game, double dt, int screenW, int screenH) {
    auto& vn = state.vnState;
    if (!vn.active) return;
    
    // Check if new system visited for standard post-tutorial hint
    if (vn.tutorialCompleted) {
        if (game.playerAgent >= 0 && game.playerAgent < (int)game.agents.size()) {
            int currentStar = game.agents[game.playerAgent].currentStar;
            if (currentStar >= 0 && vn.visitedSystems.find(currentStar) == vn.visitedSystems.end()) {
                vn.visitedSystems.insert(currentStar);
                vn.tutorialStep = 8; // we'll reuse step 8, 9, 12 for the post-tutorial message
                // Actually, wait, let's just create a custom post-tutorial state sequence.
                // Let's reset it to step 100 for post tutorial
                vn.tutorialStep = 100;
                vn.targetText = getTutorialText(game, 8, vn.arrowTarget, vn.tutorialCompleted); // dummy bool
                bool dummy;
                std::string p1 = getTutorialText(game, 8, vn.arrowTarget, dummy);
                std::string p2 = getTutorialText(game, 9, vn.arrowTarget, dummy);
                vn.targetText = p1 + " " + p2;
                vn.textProgress = 0.0f;
                vn.currentText = "";
                vn.active = true;
            }
        }
    } else {
        // Initial setup for tutorial
        if (vn.targetText.empty() && vn.tutorialStep == 0) {
            bool dummyOpen = false;
            vn.targetText = getTutorialText(game, vn.tutorialStep, vn.arrowTarget, dummyOpen);
        }
    }
    
    if (vn.textProgress < vn.targetText.length()) {
        vn.textProgress += dt * 50.0f; // 50 chars per second
        if (vn.textProgress > vn.targetText.length()) vn.textProgress = vn.targetText.length();
        vn.currentText = vn.targetText.substr(0, (size_t)vn.textProgress);
    }
}

"""

# Let's insert the code
content = content[:insert_idx] + new_code + content[insert_idx:]

with open("ui.cpp", "w") as f:
    f.write(content)

