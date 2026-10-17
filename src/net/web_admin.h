#pragma once

class SettingsStore;
class QuoteStore;

namespace web_admin {

void begin(SettingsStore* settings, QuoteStore* quotes);

}  // namespace web_admin
