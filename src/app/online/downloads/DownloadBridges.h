#pragma once

class AddonManager;
class DownloadCenter;
class UpdateChecker;

// Mirrors transfers that live in other classes into the DownloadCenter. Those classes keep
// ownership of the transfer; the center only shows it and forwards cancel and retry.
void bridgeUpdates(DownloadCenter *center, UpdateChecker *updates);
void bridgeAddons(DownloadCenter *center, AddonManager *addons);
