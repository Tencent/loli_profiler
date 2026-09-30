#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <map>
#include <string>

// Qt-free application settings store (replaces QSettings for the keys the
// Qt build persisted: AndroidSDK, AndroidNDK, PythonPath, theme,
// last-open-dir, last-symbol-dir). JSON-backed via the vendored rapidjson
// (thirdparty/rapidjson).
//
// On Windows QSettings used the registry; the spec requires a per-user
// config file instead. Location: a "loli_settings.json" next to the
// executable (macOS app bundles use ~/Library/Application Support/LoliProfiler
// so settings cannot invalidate the signed bundle; portable binaries match the app's
// existing convention of keeping loli3.conf/cache/ next to the binary).
//
// Usage mirrors QSettings call sites:
//   AppSettings settings;                 // loads the file
//   std::string sdk = settings.Get("AndroidSDK", "");
//   settings.Set("AndroidSDK", sdk);
//   settings.Sync();                      // persist (or rely on destructor)
class AppSettings {
public:
    // Uses the portable executable directory or the macOS bundle's user state directory.
    AppSettings();
    // Explicit path (used by tests).
    explicit AppSettings(const std::string& filePath);
    ~AppSettings();

    AppSettings(const AppSettings&) = delete;
    AppSettings& operator=(const AppSettings&) = delete;

    // Returns the value for key, or fallback when unset (QSettings::value
    // parity, including its "empty string means unset" usage pattern).
    std::string Get(const std::string& key, const std::string& fallback = "") const;

    // Overwrites the value for key (QSettings::setValue parity).
    void Set(const std::string& key, const std::string& value);

    bool Has(const std::string& key) const;
    void Remove(const std::string& key);

    // Writes pending changes to disk. The destructor also syncs.
    void Sync();

    const std::string& GetFilePath() const { return filePath_; }

private:
    void Load();
    std::string filePath_;
    std::map<std::string, std::string> values_;
    bool dirty_ = false;
};

#endif // APPSETTINGS_H
