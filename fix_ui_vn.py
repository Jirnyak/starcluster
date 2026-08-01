import sys

with open("ui.cpp", "r") as f:
    content = f.read()

sig = "std::string getTutorialText(const Game& game, int step, int& outArrowTarget, bool& outOpenTrade) {"
idx = content.find(sig)

new_code = """std::string getTutorialText(const Game& game, int step, int& outArrowTarget, bool& outOpenTrade) {
    outArrowTarget = 0;
    outOpenTrade = false;
    switch (step) {
        case 0: return "Master, I am Timertia - your AI core Agent.";
        case 1: return "Congratulations with obtaining your trading Licence!";
        case 2: outArrowTarget = 1; return "You can view your balance here.";
        case 3: return "You own 1 space ship unit for now.";
        case 4: outArrowTarget = 2; return "My subagents will monitor its system states here.";
        case 5: {
            std::string starName = "Unknown Node";
            if (game.playerAgent >= 0 && game.playerAgent < (int)game.agents.size()) {
                int starId = game.agents[game.playerAgent].currentStar;
                if (starId >= 0 && starId < (int)game.cluster.stars.size()) {
                    starName = game.cluster.stars[starId].name;
                }
            }
            outArrowTarget = 0;
            char buf[256];
            std::snprintf(buf, sizeof(buf), "Your vessel is curretnly at %s. You can acess a model of local star system here.", starName.c_str());
            return buf;
        }
        case 6: outArrowTarget = 0; outOpenTrade = true; return "With your trading licence you can perform HIGH-FREQUENCY BROKERAGE on local market.";
        case 7: return "A periodic table based on standard supersymmetrical model is common CONVENTION of interstellar market.";
        case 8: return "NASH EQUILIBRIUM proofs that it is the best to buy on suply and sell on demand.";
        case 9: {
            std::string element = "isotopes";
            if (game.playerAgent >= 0 && game.playerAgent < (int)game.agents.size()) {
                int starId = game.agents[game.playerAgent].currentStar;
                if (starId >= 0 && starId < (int)game.markets.size()) {
                    const Market& m = game.markets[starId];
                    int bestEl = -1;
                    double maxSupply = -1.0;
                    for (int i=0; i<(int)elementCount(); ++i) {
                        if (i < (int)m.supply.size() && m.supply[i].amount > maxSupply) {
                            maxSupply = m.supply[i].amount;
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
            std::snprintf(buf, sizeof(buf), "Local model suggest you to buy %s.", element.c_str());
            return buf;
        }
        case 10: {
            std::string starName = "an adjacent node";
            if (game.playerAgent >= 0 && game.playerAgent < (int)game.agents.size()) {
                int starId = game.agents[game.playerAgent].currentStar;
                if (starId >= 0 && starId < (int)game.cluster.stars.size() && starId < (int)game.markets.size()) {
                    const ClusterStar& s = game.cluster.stars[starId];
                    const Market& m = game.markets[starId];
                    int bestEl = -1;
                    double maxSupply = -1.0;
                    for (int i=0; i<(int)elementCount(); ++i) {
                        if (i < (int)m.supply.size() && m.supply[i].amount > maxSupply) {
                            maxSupply = m.supply[i].amount;
                            bestEl = i;
                        }
                    }
                    if (bestEl >= 0) {
                        int bestStar = -1;
                        double maxDemand = -1.0;
                        for (int i=0; i<(int)game.cluster.stars.size(); ++i) {
                            if (i == starId) continue;
                            if (i >= (int)game.markets.size()) continue;
                            double dx = game.cluster.stars[i].x - s.x;
                            double dy = game.cluster.stars[i].y - s.y;
                            double dz = game.cluster.stars[i].z - s.z;
                            double dist = std::sqrt(dx*dx + dy*dy + dz*dz);
                            if (dist < 15.0) {
                                if (bestEl < (int)game.markets[i].demand.size() && game.markets[i].demand[bestEl].amount > maxDemand) {
                                    maxDemand = game.markets[i].demand[bestEl].amount;
                                    bestStar = i;
                                }
                            }
                        }
                        if (bestStar >= 0) {
                            starName = game.cluster.stars[bestStar].name;
                        }
                    }
                }
            }
            char buf[256];
            std::snprintf(buf, sizeof(buf), "We also have insight that the best place to sell it right now is %s.", starName.c_str());
            return buf;
        }
        case 11: return "By the way, you can also upgrade your vessel and purchase more trading licenses.";
        case 12: return "Finally, new technology of applied color supercondctivity has developed novel AI cores.";
        case 13: outArrowTarget = 0; return "They are still prototypes and very rare. Be sure to privatise all you finde.";
        case 14: return "I am at your service with more insignts at any time Master [V].";
        case 100: {
"""

# Now we need to properly replace `getTutorialText` in `ui.cpp`.
# We'll just slice string based on case 100's closing brace.
end_idx = content.find("        case 100: {", idx)

if end_idx != -1:
    content = content[:idx] + new_code + content[end_idx + len("        case 100: {"):]
    with open("ui.cpp", "w") as f:
        f.write(content)
else:
    print("Failed to find case 100")
