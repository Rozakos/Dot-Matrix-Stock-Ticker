#pragma once

class SettingsStore;
class QuoteStore;

namespace fetcher {

// Fetches a quote for every configured symbol (one HTTPS request each) and
// publishes the batch to `store`. Returns true if the batch was published
// (even if some individual symbols failed — those land with fresh=false).
// Returns true when at least one symbol was fetched; a symbol that fails keeps
// its previous good quote (with its old updatedMs) instead of turning into
// "no data".
bool fetchQuotes(SettingsStore& settings, QuoteStore& store);

// True if the last fetchQuotes() got ANY HTTP response (any status > 0, even
// 401/500). Drives the net-task watchdog: only a transport that answers
// nothing at all (lwIP wedged with WiFi still associated) triggers a reboot,
// so a bad token can never boot-loop the device.
bool sawHttpResponseThisCycle();

}  // namespace fetcher
