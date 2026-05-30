#pragma once

class SettingsStore;
class QuoteStore;

namespace fetcher {

// Fetches a quote for every configured symbol (one HTTPS request each) and
// publishes the batch to `store`. Returns true if the batch was published
// (even if some individual symbols failed — those land with fresh=false).
bool fetchQuotes(SettingsStore& settings, QuoteStore& store);

}  // namespace fetcher
