import sys

with open("ui.cpp", "r") as f:
    content = f.read()

sig = "void drawVisualNovel(SDL_Renderer* renderer, const WindowState& state, int screenW, int screenH, SDL_Texture* tex) {"
idx = content.find(sig)
if idx == -1:
    print("Could not find drawVisualNovel")
    sys.exit(1)

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
            outArrowTarget = 3;
            char buf[256];
            std::snprintf(buf, sizeof(buf), "Your vessel is curretnly at %s. You can acess a model of local star system here.", starName.c_str());
            return buf;
        }
        case 6: outArrowTarget = 4; outOpenTrade = true; return "With your trading licence you can perform HIGH-FREQUENCY BROKERAGE on local market.";
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
        case 13: outArrowTarget = 2; return "They are still prototypes and very rare. Be sure to privatise all you finde.";
        case 14: return "I am at your service with more insignts at any time Master [V].";
        case 100: {
            std::string element = "isotopes";
            std::string starName = "an adjacent node";
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
                        
                        const ClusterStar& s = game.cluster.stars[starId];
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
            char buf[512];
            std::snprintf(buf, sizeof(buf), "Local model suggest you to buy %s. We also have insight that the best place to sell it right now is %s.", element.c_str(), starName.c_str());
            return buf;
        }
    }
    return "";
}

bool advanceVisualNovel(WindowState& state, Game& game, int winW, int winH) {
    auto& vn = state.vnState;
    if (!vn.active) return false;
    
    if (vn.textProgress < vn.targetText.length()) {
        vn.textProgress = vn.targetText.length();
        vn.currentText = vn.targetText;
    } else {
        if (!vn.tutorialCompleted) {
            vn.tutorialStep++;
            if (vn.tutorialStep > 14) {
                vn.tutorialCompleted = true;
                vn.active = false;
            } else {
                bool openTrade = false;
                vn.targetText = getTutorialText(game, vn.tutorialStep, vn.arrowTarget, openTrade);
                vn.textProgress = 0.0f;
                if (openTrade && game.playerAgent >= 0 && game.playerAgent < (int)game.agents.size()) {
                    openTradeWindow(state, game.agents[game.playerAgent].currentStar, winW, winH);
                }
            }
        } else {
            vn.active = false;
        }
    }
    return true;
}

void updateVisualNovel(WindowState& state, Game& game, double dt, int screenW, int screenH) {
    auto& vn = state.vnState;
    if (!vn.active) return;
    
    if (vn.tutorialCompleted) {
        if (game.playerAgent >= 0 && game.playerAgent < (int)game.agents.size()) {
            int currentStar = game.agents[game.playerAgent].currentStar;
            if (currentStar >= 0 && vn.visitedSystems.find(currentStar) == vn.visitedSystems.end()) {
                vn.visitedSystems.insert(currentStar);
                vn.tutorialStep = 100;
                bool dummy;
                vn.targetText = getTutorialText(game, 100, vn.arrowTarget, dummy);
                vn.textProgress = 0.0f;
                vn.currentText = "";
                vn.active = true;
            }
        }
    } else {
        if (vn.targetText.empty() && vn.tutorialStep == 0) {
            bool dummyOpen = false;
            vn.targetText = getTutorialText(game, vn.tutorialStep, vn.arrowTarget, dummyOpen);
        }
    }
    
    if (vn.textProgress < vn.targetText.length()) {
        vn.textProgress += dt * 50.0f;
        if (vn.textProgress > vn.targetText.length()) vn.textProgress = vn.targetText.length();
        vn.currentText = vn.targetText.substr(0, (size_t)vn.textProgress);
    }
}

std::string wrapText(const std::string& text, int maxChars) {
    std::string result;
    int lineLen = 0;
    std::string word;
    
    for (char c : text) {
        if (c == ' ' || c == '\\n') {
            if (lineLen + word.length() > maxChars) {
                result += "\\n";
                lineLen = 0;
            } else if (!result.empty() && result.back() != '\\n') {
                result += " ";
                lineLen++;
            }
            result += word;
            lineLen += word.length();
            word.clear();
            if (c == '\\n') {
                result += "\\n";
                lineLen = 0;
            }
        } else {
            word += c;
        }
    }
    if (!word.empty()) {
        if (lineLen + word.length() > maxChars && !result.empty() && result.back() != '\\n') {
            result += "\\n";
        } else if (!result.empty() && result.back() != '\\n') {
            result += " ";
        }
        result += word;
    }
    return result;
}

"""
content = content[:idx] + new_code + content[idx:]

old_draw = "    drawText(renderer, boxX + 20, boxY + 60, state.vnState.currentText, {214, 228, 238, 255}, 2);"
new_draw = """    int maxChars = (boxW - 40) / 12;
    std::string wrapped = wrapText(state.vnState.currentText, maxChars);
    drawText(renderer, boxX + 20, boxY + 60, wrapped, {214, 228, 238, 255}, 2);
    
    int ax = 0, ay = 0;
    if (state.vnState.arrowTarget == 1) { ax = 350; ay = 94; }
    else if (state.vnState.arrowTarget == 2) { ax = 350; ay = 148; }
    else if (state.vnState.arrowTarget == 3) { ax = 350; ay = 278; }
    else if (state.vnState.arrowTarget == 4) { ax = screenW / 2 + 150; ay = 100; }
    else if (state.vnState.arrowTarget == 5) { ax = 350; ay = 250; }
    
    if (ax > 0 && ay > 0) {
        if ((SDL_GetTicks() / 300) % 2 == 0) {
            drawText(renderer, ax, ay, "<-- TARGET", {255, 100, 100, 255}, 2);
        }
    }"""
content = content.replace(old_draw, new_draw)

with open("ui.cpp", "w") as f:
    f.write(content)
