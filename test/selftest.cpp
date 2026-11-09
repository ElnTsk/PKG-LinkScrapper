#include <cstdio>
#include <string>
#include <vector>

#include "../src/config.h"
#include "../src/catalog.h"
#include "../src/hosts.h"
#include "../src/html.h"
#include "../src/json.h"
#include "../src/match.h"
#include "../src/parts.h"
#include "../src/util.h"

namespace {

std::string encodeBase64Url(const std::string& bytes) {
  static const char* alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  unsigned int buffer = 0;
  int bits = 0;
  std::string out;
  for (unsigned char c : bytes) {
    buffer = (buffer << 8) | c;
    bits += 8;
    while (bits >= 6) {
      bits -= 6;
      out.push_back(alphabet[(buffer >> bits) & 0x3F]);
      buffer &= (1u << bits) - 1u;
    }
  }
  if (bits > 0) out.push_back(alphabet[(buffer << (6 - bits)) & 0x3F]);
  return out;
}

int checks = 0;
int failures = 0;

void check(bool condition, const std::string& label) {
  ++checks;
  if (!condition) {
    ++failures;
    std::printf("FAIL %s\n", label.c_str());
  }
}

void checkEq(const std::string& actual, const std::string& expected, const std::string& label) {
  ++checks;
  if (actual != expected) {
    ++failures;
    std::printf("FAIL %s\n  expected: %s\n  actual:   %s\n", label.c_str(), expected.c_str(),
                actual.c_str());
  }
}

void checkUrl(const std::string& base, const std::string& reference, const std::string& expected) {
  check(resolveUrl(base, reference) == expected, base + " + " + reference + " -> " + expected);
}

void jsonCases() {
  JsonValue value;
  std::string error;
  const char* text =
      "{\"defaults\": {\"match_threshold\": 0.5, \"verify\": false, \"depth\": 2},"
      "\"disable\": [\"psxhax\"],"
      "\"sources\": [{\"name\": \"a\", \"type\": \"index\", \"urls\": [\"https://x.test/{q}\", \"https://y.test\"]}]}";
  bool parsed = parseJson(text, value, error);
  check(parsed, "json parses: " + error);
  if (!parsed) return;

  const JsonValue* defaults = value.find("defaults");
  check(defaults != nullptr, "defaults present");
  if (!defaults) return;
  check(defaults->find("match_threshold")->asDouble(0) == 0.5, "nested number");
  check(defaults->find("verify")->asBool(true) == false, "nested bool");
  check(value.find("disable")->asStringList().size() == 1, "string array");
  check(value.find("sources")->array.front().find("urls")->asStringList().size() == 2, "source urls");

  JsonValue broken;
  check(!parseJson("{oops}", broken, error), "rejects malformed json");
  check(broken.isNull(), "broken parse leaves null");
}

void classifyCases() {
  check(classify("https://1fichier.com/?gjba0jrd0ic5sa91qnr2") == "filehost", "1fichier query link");
  check(classify("https://buzzheavier.com/eqfjuyld9vxm") == "filehost", "single segment share id");
  check(classify("https://vikingfile.com/f/Yx2oOmhZWA") == "filehost", "/f/ share marker");
  check(classify("https://drive.google.com/file/d/1HKOnFAtu/view") == "filehost", "gdrive file path");
  check(classify("https://downloadgameps3.net/archives/32230") == "skip",
        "cloudflare-blocked mirror is skipped");
  check(classify("https://1fichier.com/guide/how-to-download") == "page",
        "guide page on a file host is not a file");
  check(classify("https://akirabox.to/Marvels.Spider.Man.2.jpg") == "skip",
        "cover art on a file host is skipped");
  check(classify("https://dlpsgame.com/elden-ring-free-ps5/") == "page", "release page");
  check(classify("https://cdn.example.com/game.pkg") == "direct", "direct pkg");
  check(classify("https://dl2.dlget.ir/users/alireza/ps5/Elden%20Ring/UP0700-PPSA04610.pkg") ==
            "direct",
        "psn pkg part");
  check(classify("https://dl0.thesuperboffers.com/users/nariman/PS5-Hacked/Elden%20Ring/x.part01.rar") ==
            "filehost",
        "multi part rar set");
  check(classify("https://filecrypt.cc/Container/B07A100C2E.html") == "container", "container page");
  check(classify("https://tpi.li/aAtUSsNRVK") == "short", "link locker");
  check(classify("https://en.wikipedia.org/wiki/Elden_Ring") == "skip", "blocked host");
  check(classify("javascript:void(0)") == "skip", "non http");
}

void unwrapCases() {
  const std::string target = "https://www.eldeNring.com/";
  std::string encoded = encodeBase64Url(target);
  const std::string longTarget =
      "https://dlpsgame.com/marvels-spider-man-2-deluxe-edition-ps4-pkg-download-page/";
  std::string longEncoded = encodeBase64Url(longTarget);

  checkEq(base64UrlDecode("aHR0cDovL2EudGVzdC9i"), "http://a.test/b", "base64UrlDecode known vector");
  checkEq(base64UrlDecode(encoded), target, "base64UrlDecode round trip");
  checkEq(base64UrlDecode(longEncoded), longTarget, "base64UrlDecode long url");
  checkEq(base64UrlDecode(encodeBase64Url("a")), "a", "single byte");
  checkEq(base64UrlDecode(encodeBase64Url("ab")), "ab", "two bytes");
  checkEq(base64UrlDecode(encodeBase64Url("abc")), "abc", "three bytes");
  checkEq(base64UrlDecode(encodeBase64Url("abcd")), "abcd", "four bytes");

  std::string bingHref = "https://www.bing.com/ck/a?!&&p=deadbeef&u=a1" + encoded + "&ntb=1";
  check(unwrapBingHref(bingHref) == target, "bing ck/a unwrap");
  std::string bingLong = "https://www.bing.com/ck/a?!&&p=deadbeef&u=a1" + longEncoded + "&ntb=1";
  check(unwrapBingHref(bingLong) == longTarget, "bing ck/a unwrap long");
  check(unwrapBingHref("https://dlpsgame.com/elden-ring-ps5/") == "https://dlpsgame.com/elden-ring-ps5/",
        "non-bing href passes through");
  check(unwrapBingHref("https://www.bing.com/search?q=x").empty(), "bing search link is dropped");

  std::string ddgHref = "//duckduckgo.com/l/?uddg=https%3A%2F%2Fexample.com%2Fa&rut=abc";
  check(unwrapDuckDuckGoHref(ddgHref) == "https://example.com/a", "ddg uddg unwrap");
  check(unwrapDuckDuckGoHref("https://example.com/z") == "https://example.com/z",
        "ddg external href passes through");

  check(queryParam("a=1&uddg=hello%20world&b=2", "uddg") == "hello%20world", "queryParam");
  check(queryParam("a=1", "b").empty(), "queryParam missing");
}

void utilCases() {
  check(normalizeText("Elden  Ring: SHADOW of the Erdtree") == "elden ring shadow of the erdtree",
        "normalizeText");
  check(normalizeText("God of War Ragnarök PS5") == "god of war ragnarok ps5", "accent folding");
  check(percentEncode("a b&c") == "a+b%26c", "percentEncode");
  check(decodeEntities("a &amp; b &#39;c&#39; &lt;x&gt;") == "a & b 'c' <x>", "decodeEntities");
  check(registrable("www.drive.google.com") == "google.com", "registrable www");
  check(registrable("a.b.example.co.uk") == "example.co.uk", "registrable co.uk");
  check(normalizeUrl("https://example.com/a/?utm_source=x&ref=y") == "https://example.com/a",
        "normalizeUrl strips trackers");
  check(normalizeUrl("https://example.com:443/a/b/") == "https://example.com/a/b", "normalizeUrl port+slash");
  check(formatBytes(5ULL << 30) == "5.0GB", "formatBytes");
  check(truncateTo("abcdefghij", 8) == "abcde...", "truncateTo");

  UrlParts parts = crackUrl("https://example.com:8443/dir/file.pkg?q=1#frag");
  check(parts.valid && parts.scheme == "https" && parts.host == "example.com" && parts.port == 8443,
        "crackUrl authority");
  check(parts.path == "/dir/file.pkg" && parts.query == "q=1" && parts.fragment == "frag",
        "crackUrl path/query/fragment");

  checkUrl("https://a.test/dir/page/", "/other/x", "https://a.test/other/x");
  checkUrl("https://a.test/dir/page/", "rel", "https://a.test/dir/page/rel");
  checkUrl("https://a.test/dir/page/", "../up", "https://a.test/dir/up");
  checkUrl("https://a.test/dir/page", "rel", "https://a.test/dir/rel");
  checkUrl("https://a.test/dir/page/", "//b.test/z", "https://b.test/z");
  checkUrl("https://a.test/dir/page/", "?q=2", "https://a.test/dir/page/?q=2");
}

void matchCases() {
  check(scoreTitle("elden ring", "Elden Ring PS5 PKG") >= 0.9, "containment at start");
  check(scoreTitle("spider-man 2", "Marvels Spider Man 2 Deluxe Edition PS5") >= 0.9,
        "single leading word");
  check(scoreTitle("spider-man 2", "The Amazing Spider Man 2 - Download Game PS4") < 0.72,
        "two leading words is a different game");
  check(scoreTitle("elden ring", "God of War Ragnarok PS4") < 0.5, "unrelated titles");
  check(scoreTitle("Astrobot", "ASTRO BOT") >= 0.9, "compound query meets split title");
  check(scoreTitle("astro bot", "AstroBot") >= 0.9, "split query meets compound title");
  check(scoreTitle("astrobot", "Astro Bot Deluxe Edition") >= 0.9,
        "compound query meets longer split title");
  check(scoreTitle("spiderman 2", "Spider-Man 2") >= 0.9, "compound plus number");
  check(scoreTitle("gtav", "GTA V") >= 0.9, "short compound still matches");
  check(scoreTitle("astrobot", "Astro Boy") < 0.72, "a merged word that differs stays out");
  check(scoreTitle("ratchet clank", "Ratchet & Clank Rift Apart") >= 0.9,
        "compound inside a longer title");
  check(detectConsole("Elden Ring PPSA04610") == "PS5", "ppsa means ps5");
  check(detectConsole("Elden Ring CUSA12345") == "PS4", "cusa means ps4");
  check(detectConsole("Spider Man 2 PS4 FPKG") == "PS4", "fpkg means ps4");
  check(detectConsole("Backport PS4 and PS5") == "", "both consoles is ambiguous");
  check(foreignPlatform("Spider Man 2 Enter Electro - Download Game PSX PS2 PS3") == "legacy",
        "legacy platform");
  check(foreignPlatform("Spider Man 2 - Download Game PSX PS2 PS3 PS4 PS5") == "",
        "console present cancels legacy flag");
  check(slugHasPlatform("spider-man-2-ps4-pkg"), "slug has platform");
  check(!slugHasPlatform("elden-ring-the-meow-nauts"), "slug without platform");
  check(isGenericLabel("Download") && isGenericLabel("1File"), "generic labels");
  check(isGenericLabel("https://1fichier.com/?abc"), "bare url anchor is generic");
  check(!isGenericLabel("Elden Ring PS5 part 1"), "specific label");

  // the shop's button text must not become the row title
  check(isHostLabel("Gofile") && isHostLabel("Mirror") && isHostLabel("1File (Multi)"),
        "host labels recognised");
  check(isHostLabel("Download via Mega") && isHostLabel("Gofile / Mediafire"), "brand in a short label");
  check(!isHostLabel("Call of Duty Black Ops - Download Game PSX PS2 PS3 PS4 PS5"),
        "a release title is not a shop name");
  check(!isHostLabel(""), "empty label is not a shop name");
  {
    const std::string page = "Call of Duty Black Ops - Download Game PS4";
    check(titleFromLink(page, "Gofile") == page, "shop label falls back to the page title");
    check(titleFromLink(page, "Mirror") == page, "mirror label falls back too");
    check(titleFromLink(page, "1File (Multi)") == page + " [1File (Multi)]",
          "a label that names the parcel is kept as a tag");
    check(titleFromLink(page, "Elden Ring Update v1.21") == "Elden Ring Update v1.21",
          "a specific anchor keeps its own words");
    check(titleFromLink(page, "") == page, "no anchor text uses the page title");
  }
  check(isBareHostRoot("https://mega.nz/"), "bare host root");
  check(!isBareHostRoot("https://mega.nz/file/abcd"), "file path is not root");
  check(!isBareHostRoot("https://1fichier.com/?abc123"), "query root is not bare");
  check(looksLikeHandler("https://www.superpsx.com/dll-gowrvps4/"), "download handler");
  check(!looksLikeHandler("https://www.superpsx.com/god-of-war-ragnarok-ps5-game/"), "release slug");
  check(isBlockedHost("en.wikipedia.org") && !isBlockedHost("dlpsgame.com"), "blocked hosts");
  check(titleFromUrl("https://a.test/elden-ring-ps5/") == "elden ring ps5", "titleFromUrl");
}

void htmlCases() {
  const char* html =
      "<html><head><title>Elden Ring PS5 PKG</title></head><body>"
      "<a href=\"/relative/path\">rel</a>"
      "<a class=\"result-link\" href=\"https://1fichier.com/?abc&amp;x=1\">part 1</a>"
      "<a href=\"#top\">top</a>"
      "<a href=\"javascript:void(0)\">js</a>"
      "<p>mirror: https://cdn.test/game.pkg and text</p>"
      "</body></html>";
  Page page = extractPage(html, "https://dlpsgame.com/elden-ring-free-ps5/");
  check(page.title == "Elden Ring PS5 PKG", "page title");
  check(page.links.size() == 3, "link count");
  check(page.links[0].href == "https://dlpsgame.com/relative/path", "relative resolution");
  check(page.links[1].href == "https://1fichier.com/?abc&x=1", "entity decode in href");
  check(page.links[1].text == "part 1", "anchor text");
  check(page.links[2].href == "https://cdn.test/game.pkg", "bare pkg discovered");

  std::vector<RawAnchor> anchors = rawAnchors(html);
  check(anchors.size() == 4, "raw anchors keep javascript and fragment links");
  check(anchors[1].className == "result-link", "class captured");

  // the walk a 1fichier share page needs: the mirror link it prints when its
  // own form is posted, and the filename it puts in a class rather than a link
  const char* formReply =
      "<div class=\"acc-hint\">Free - with waiting time, limited speed and ads</div>"
      "<span class=\"tier-name\">[DLPSGAME.COM] - 01.018 PPSA21567 FIXED.rar</span>"
      "<a href=\"https://1fichier.com/cgu.html\">terms</a>"
      "<a href=\"http://c-4.1fichier.com/c1282791184\" class=\"ok btn-general\">Start your download</a>";
  check(mirrorLink(formReply, "1fichier.com") == "http://c-4.1fichier.com/c1282791184",
        "mirror link found past the hyphen in the label");
  check(classText(formReply, "tier-name") == "[DLPSGAME.COM] - 01.018 PPSA21567 FIXED.rar",
        "filename read out of the page");
  const char* decoy =
      "<a href=\"https://1fichier.com/login.pl\">login</a>"
      "<a href=\"http://evil.com/c-4.1fichier.com/x\">lookalike</a>"
      "<a href=\"http://cdx.1fichier.com/x\">not a mirror</a>";
  check(mirrorLink(decoy, "1fichier.com").empty(), "the site's own pages are not mirrors");
  check(mirrorLink("<a href=\"//d1.1fichier.com/abc123\">go</a>", "1fichier.com") ==
            "http://d1.1fichier.com/abc123",
        "protocol-relative mirror link");
  check(mirrorLink("<a href=\"http://c-4.1fichier.com/\">empty token</a>", "1fichier.com").empty(),
        "a mirror with no path is no link");

  const char* og = "<html><head><meta property=\"og:title\" content=\"God of War PS4\"></head></html>";
  check(extractPage(og, "https://a.test/").title == "God of War PS4", "og:title fallback");

  const char* catalog =      "[{\"name\":\"Elden Ring\",\"download\":\"https://dl2.dlget.ir/ps5/Elden%20Ring/game.pkg\"},"
      "{\"title\":\"Horizon Chase\",\"links\":[\"https://1fichier.com/?abc\",\"not-a-url\"]},"
      "{\"nested\":{\"items\":[{\"label\":\"God of War\",\"url\":\"https://mega.nz/file/xyz\"}]}}]";
  check(looksLikeJson(catalog), "json body detected");
  check(!looksLikeJson("<html><body>hi</body></html>"), "html body not json");
  Page json = extractPage(catalog, "https://catalog.test/list.json");
  check(json.links.size() == 3, "json url extraction count");
  check(json.links[0].href == "https://dl2.dlget.ir/ps5/Elden%20Ring/game.pkg", "json download url");
  check(json.links[0].text == "Elden Ring", "json name carried as anchor text");
  check(json.links[1].text == "Horizon Chase", "nested array url inherits name");
  check(json.links[2].href == "https://mega.nz/file/xyz", "deep nested url found");
  check(json.links[2].text == "God of War", "deep nested label found");

  const std::string hidden =
      "<a class=\"button\" data-payload=\"" +
      encodeBase64Url("<p>mirror</p><a href=\"https://buzzheavier.com/eqfjuyld9vxm\">DL</a>"
                      "<a href=\"https://vikingfile.com/f/Yx2oOmhZWA\">DL2</a>") +
      "\">Loading...</a>";
  Page payload = extractPage(hidden, "https://dlpsgame.com/elden-ring-free-ps5/");
  check(payload.links.size() == 2, "base64 data-payload unwrapped");
  check(payload.links[0].href == "https://buzzheavier.com/eqfjuyld9vxm", "decoded anchor recovered");
  check(payload.links[0].text == "DL", "decoded anchor text kept");
  check(looksBase64(encodeBase64Url("0123456789abcdefghij")), "base64 detection");
  check(!looksBase64("https://example.com/a"), "url is not base64");
}

}  // namespace

namespace {

Hit makeHit(const std::string& url, const std::string& state, long long bytes) {
  Hit hit;
  hit.game = "elden ring";
  hit.console = "ps5";
  hit.kind = "direct";
  hit.url = url;
  hit.state = state;
  hit.sizeBytes = bytes;
  hit.size = bytes > 0 ? formatBytes(bytes) : std::string();
  hit.host = crackUrl(url).host;
  return hit;
}

void partsCases() {
  const std::string setBase =
      "https://dl0.thesuperboffers.com/PS5-Hacked/Elden%20Ring%20Nightreign/ELDEN%20RING%20NIGHTREIGN"
      "%2001.000%20PPSA25381%20-www.P30DAY.ir.part";

  check(isPartUrl(setBase + "01.rar"), "part01 detected");
  check(partIndexOf(setBase + "01.rar") == 1, "part01 index");
  check(partIndexOf(setBase + "21.rar") == 21, "part21 index");
  check(isPartUrl(setBase + "10.rar"), "part10 detected");
  check(!isPartUrl("https://dl0.thesuperboffers.com/PS5-Hacked/Elden%20Ring%20Nightreign/EldenRing_4xx"
                   "%5BPPSA25381%5D%5B01.000.000%5D-www.P30Day.ir.rar"),
        "whole archive is not a part");
  check(!isPartUrl("https://archive.org/download/Bloodborne.Game.of.the.Year.Edition.PS4-PRELUDE.pkg"),
        "single pkg is not a part");
  check(!isPartUrl("https://archive.org/download/CUSA03173-EUR-Update-v1.09-PRELUDE-%5BPS4ID%5D.pkg"),
        "update pkg is not a part");
  check(!isPartUrl("https://mega.nz/file/xyz"), "share link is not a part");

  check(isPartUrl("https://cdn.test/ps4/game.001") && partIndexOf("https://cdn.test/ps4/game.002") == 2,
        "numeric split detected");
  check(isPartUrl("https://cdn.test/ps4/game.z03") && partIndexOf("https://cdn.test/ps4/game.z03") == 3,
        "z-split detected");
  check(!isPartUrl("https://cdn.test/ps4/game.zip"), "zip is not a part");
  check(isPartUrl("https://cdn.test/PS5/ELDEN_00-ELDENRING_1.pkg") &&
            partIndexOf("https://cdn.test/PS5/ELDEN_00-ELDENRING_1.pkg") == 1,
        "pkg part detected");

  std::vector<Hit> hits;
  for (int index = 1; index <= 21; ++index) {
    char label[8];
    std::snprintf(label, sizeof(label), "%02d", index);
    hits.push_back(makeHit(setBase + std::string(label) + ".rar", index == 11 ? "alive" : "stub",
                           50000000));
  }
  hits.push_back(makeHit("https://cdn.test/ps4/other.pkg", "alive", 1000));

  std::vector<Hit> rows = groupParts(hits);
  check(rows.size() == 2, "21 parts plus a loose file is 2 rows");
  check(rows[0].partCount == 21, "set carries a count");
  check(rows[0].partUrls.size() == 21, "set keeps every member url");
  check(rows[0].url == setBase + "01.rar", "row url is the first part");
  check(rows[0].state == "alive", "one live part lifts the set");
  check(rows[0].sizeBytes == 21LL * 50000000, "sizes sum across the set");
  check(partsLabel(rows[0]) == "21 parts", "parts label");
  check(rows[1].partCount == 0 && rows[1].partUrls.empty(), "loose file stays a loose file");
  check(partsLabel(rows[1]).empty(), "loose file has no label");

  std::vector<Hit> splitStates;
  splitStates.push_back(makeHit(setBase + "01.rar", "dead", 1));
  splitStates.push_back(makeHit(setBase + "02.rar", "unreachable", 1));
  std::vector<Hit> partial = groupParts(splitStates);
  check(partial.size() == 1 && partial[0].state == "partial", "no live part reads as partial");

  std::vector<Hit> otherFolder;
  otherFolder.push_back(makeHit("https://dl0.thesuperboffers.com/PS4-Hacked/Elden%20Ring/"
                                "ELDEN%20RING%2001.000%20PPSA25381%20-www.P30DAY.ir.part01.rar",
                                "alive", 10));
  otherFolder.push_back(makeHit(setBase + "01.rar", "alive", 10));
  check(groupParts(otherFolder).size() == 2, "same set name in another folder stays separate");

  std::vector<Hit> twoSets;
  twoSets.push_back(makeHit("https://cdn.test/ps5/TITLE-A_00-GAME_0.pkg", "alive", 5));
  twoSets.push_back(makeHit("https://cdn.test/ps5/TITLE-A_00-GAME_1.pkg", "alive", 5));
  twoSets.push_back(makeHit("https://cdn.test/ps5/TITLE-B_00-GAME_0.pkg", "alive", 5));
  std::vector<Hit> sets = groupParts(twoSets);
  check(sets.size() == 2, "two title sets stay two rows");
  check(sets[0].partCount == 2, "first set merged");

  std::vector<Hit> lone;
  lone.push_back(makeHit(setBase + "07.rar", "alive", 5));
  std::vector<Hit> loneRows = groupParts(lone);
  check(loneRows.size() == 1 && loneRows[0].partCount == 0, "a lone part is not a set");
  check(loneRows[0].url == setBase + "07.rar", "lone part keeps its url");

  std::vector<Hit> plain;
  plain.push_back(makeHit("https://1fichier.com/?abc123", "alive", 5));
  plain.push_back(makeHit("https://buzzheavier.com/eqfjuyld9vxm", "alive", 5));
  std::vector<Hit> plainRows = groupParts(plain);
  check(plainRows.size() == 2, "non-parts pass through untouched");
  check(plainRows[1].url == "https://buzzheavier.com/eqfjuyld9vxm", "order preserved");
}

void catalogCases() {
  const std::string pegasus = R"({
    "name": "PS5 Catalog",
    "version": 1,
    "packages": [
      {
        "titleId": "PPSA16280",
        "title": "Ultros",
        "posterUrl": "https://pegasus-dl.dev/api/posters/abc",
        "downloadSource": "https://dlpsgame.com/ultros-ps5/",
        "downloadLinks": [
          {"name": "USA Base - Akirabox", "url": "https://akirabox.to/9QWmpWPJNGEB/file"},
          {"name": "USA Base - Vikingfile", "url": "https://vikingfile.com/f/gD7xDVLlmo"}
        ]
      },
      {"titleId": "PPSA00001", "title": "No Links Here"}
    ]
  })";
  JsonValue doc;
  std::string error;
  check(parseJson(pegasus, doc, error), "catalog json parses: " + error);
  std::vector<CatalogEntry> entries = collectCatalogEntries(doc);
  check(entries.size() == 1, "only entries with links survive");
  check(entries[0].title == "Ultros", "pegasus title");
  check(entries[0].id == "PPSA16280", "title id kept");
  check(entries[0].pageUrl == "https://dlpsgame.com/ultros-ps5/", "download source is the page");
  check(entries[0].links.size() == 2, "two mirror links");
  check(entries[0].links[0].label == "USA Base - Akirabox", "link object name is the label");
  bool leakedPoster = false;
  for (const CatalogLink& link : entries[0].links) {
    if (link.url.find("pegasus-dl.dev") != std::string::npos) leakedPoster = true;
  }
  check(!leakedPoster, "poster urls stay out of the links");

  const std::string plain = R"([
    {
      "name": "#KILLALLZOMBIES",
      "page_url": "https://dlpsgame.com/killallzombies-ps4-download-free/",
      "download_links": {
        "mediafire": ["https://filecrypt.cc/Container/6912073BCA.html",
                      "https://dlpsgame.com/guide-to-download-at-highest-speed/"],
        "1file": ["https://1fichier.com/?htdgi43tprada3hzgc7f",
                  "https://1fichier.com/?htdgi43tprada3hzgc7f"]
      }
    }
  ])";
  JsonValue flat;
  check(parseJson(plain, flat, error), "database json parses: " + error);
  std::vector<CatalogEntry> flatEntries = collectCatalogEntries(flat);
  check(flatEntries.size() == 1, "database array entry");
  check(flatEntries[0].title == "#KILLALLZOMBIES", "database name is the title");
  check(flatEntries[0].links.size() == 3, "host buckets flattened, duplicates dropped");
  check(flatEntries[0].links[0].label.empty(), "plain url arrays carry no label");

  JsonValue rootOnly;
  check(parseJson(R"({"name": "Some Catalog", "version": 1})", rootOnly, error),
        "root-only json parses");
  check(collectCatalogEntries(rootOnly).empty(), "a named root with no links is not an entry");
}

}  // namespace

namespace {

void trustCases() {
  const std::vector<std::string> list = {"dlget.ir", "1fichier.com", "archive.org", "www.gofile.io"};

  check(hostInList("dl2.dlget.ir", list), "subdomain of a trusted host");
  check(hostInList("dlget.ir", list), "exact trusted host");
  check(hostInList("WWW.DLGET.IR", list), "case and www ignored");
  check(hostInList("ia800000.us.archive.org", list), "deep archive subdomain");
  check(hostInList("gofile.io", list), "www stripped from the list entry");
  check(!hostInList("notdlget.ir", list), "lookalike suffix rejected");
  check(!hostInList("evil.com", list), "unlisted host");
  check(!hostInList("", list), "empty host");
  check(!hostInList("dlget.ir", {}), "empty list trusts nothing");
}

}  // namespace

int main() {
  utilCases();
  unwrapCases();
  classifyCases();
  jsonCases();
  matchCases();
  htmlCases();
  partsCases();
  catalogCases();
  trustCases();
  std::printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
