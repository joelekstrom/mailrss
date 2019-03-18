#include <curl/curl.h>
#include "lib/tinyxml2/tinyxml2.h"
#include <string>
#include <fstream>
#include <streambuf>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>

using namespace std;

namespace mailrss {
    class XMLElementWrapper {
    public:
        XMLElementWrapper(tinyxml2::XMLElement *element): element(element) {}

        optional<string> textOfChildElement(string childElementName) const {
            auto childElement = element->FirstChildElement(childElementName.c_str());
            if (childElement) {
                const char *text = childElement->GetText();
                if (text) return text;
            }
            return {};
        }

        optional<string> textOfAttribute(string attributeName) {
            auto attributeText = element->Attribute(attributeName.c_str());
            if (attributeText) {
                return attributeText;
            }
            return {};
        }

        void setTextOfAttribute(string attributeName, string text) {
            element->SetAttribute(attributeName.c_str(), text.c_str());
        }
    protected:
        tinyxml2::XMLElement *element;
    };

    class Feed: XMLElementWrapper {
    public:
        class Entry: XMLElementWrapper {
        public:
            Entry(tinyxml2::XMLElement *element): XMLElementWrapper(element) {}
            optional<string> title() const { return textOfChildElement("title"); }
            optional<string> URL() const { return textOfChildElement("URL"); }
            optional<string> description() const { return textOfChildElement("description"); }
            optional<string> GUID() const {
                // Try different fields in order of preference since none is required
                auto guid = textOfChildElement("guid");
                if (guid) return guid;
                if (URL()) return URL();
                if (title()) return title();
                if (description()) {
                    // Hash the content of description as a last resort
                    hash<string> hasher;
                    size_t hash = hasher(description().value());
                    ostringstream stringStream;
                    stringStream << hash;
                    string a;
                    return stringStream.str();
                }
                return {};
            }
        };

        vector<Entry> entries;
        const optional<string> title() const { return textOfChildElement("title"); };

        // TODO: Add support for Atom documents
        static optional<Feed> parseDocument(tinyxml2::XMLDocument &document) {
            auto rss = document.FirstChildElement("rss");
            if (rss == nullptr) {
                // Not an RSS-feed, currently unsupported
                return {};
            }
            auto channelElement = rss->FirstChildElement("channel");
            return Feed(channelElement);
        }
    private:
        Feed(tinyxml2::XMLElement *channelElement): XMLElementWrapper(channelElement) {
            auto item = element->FirstChildElement("item");
            for (; item != nullptr; item = item->NextSiblingElement("item")) {
                entries.push_back(Entry(item));
            }
        }
    };

    /**
     A local feed is an entry in the OPML-file provided by the user. It will
     add the last seen state to the feed elements in the document.
     */
    class LocalFeed: XMLElementWrapper {
    public:
        LocalFeed(tinyxml2::XMLElement *element): XMLElementWrapper(element) {}

        optional<string> title() { return textOfAttribute("title"); }
        optional<string> URL() { return textOfAttribute("xmlUrl"); }

        optional<string> lastSeenEntryGUID() { return textOfAttribute("mailrssLastSeen"); }
        void setLastSeenEntryGUID(string GUID) { setTextOfAttribute("mailrssLastSeen", GUID.c_str()); }
        void remove() {
            element->GetDocument()->DeleteNode(element);
            element = nullptr;
        }

        std::vector<Feed::Entry> unseenEntriesInRemoteFeed(Feed &feed) {
            vector<Feed::Entry> unseenEntries;
            for (auto entry : feed.entries) {
                if (entry.GUID() == lastSeenEntryGUID())
                    return unseenEntries;
                unseenEntries.push_back(entry);
            }
            return unseenEntries;
        }
    };

    class OPMLParser : public tinyxml2::XMLVisitor {
    public:
        vector<LocalFeed> feeds;

        bool VisitExit(const tinyxml2::XMLElement& element) {
            const char *type = element.Attribute("type");
            if (type == nullptr || strcmp(type, "rss") != 0) return true;

            const char *title = element.Attribute("title");
            const char *URL = element.Attribute("xmlUrl");

            if (title == nullptr || URL == nullptr) {
                fprintf(stderr, "Skipping malformed feed: %s with URL: %s\n", title, URL);
                return true;
            }
            feeds.push_back(LocalFeed((tinyxml2::XMLElement *)&element));
            return true;
        }
    };

    class HTTPRequest {
    public:
        HTTPRequest(string URL) {
            this->URL = URL;
        }

        string result;
        string error;

        void perform() {
            CURL *curl = curl_easy_init();
            curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer.data());
            curl_easy_setopt(curl, CURLOPT_URL, URL.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriter);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
            curl_easy_perform(curl);
            curl_easy_cleanup(curl);

            if (strlen(errorBuffer.data()) > 0) {
                error = errorBuffer.data();
            }
        }

    private:
        vector<char> errorBuffer = vector<char>(CURL_ERROR_SIZE);
        string URL;

        static int curlWriter(char *data, size_t size, size_t nmemb, string *writerData) {
            if(writerData == NULL)
                return 0;
            writerData->append(data, size * nmemb);
            return size * nmemb;
        }
    };

    void replaceWord(string text, string word, optional<string> replacement) {
        if (!replacement)
            return;

        size_t index;
        while ((index = text.find(word)) != string::npos) {
            text.replace(index, word.length(), *replacement);
        }
    }

    string formatEmail(const Feed& feed, const Feed::Entry& entry) {
        ifstream templateFile("template.mail");
        stringstream buffer;
        buffer << templateFile.rdbuf();
        auto text = buffer.str();
        replaceWord(text, "{{feed_title}}", feed.title());
        replaceWord(text, "{{article_title}}", entry.title());
        replaceWord(text, "{{article_description}}", entry.description());
        replaceWord(text, "{{article_url}}", entry.URL());
        return text;
    };

    void sendmail(string mail) {
        FILE *mailpipe = popen("sendmail -t", "w");
        if (mailpipe != NULL) {
            fwrite(mail.c_str(), 1, mail.length(), mailpipe);
            fwrite(".\n", 1, 2, mailpipe);
            pclose(mailpipe);
        } else {
            perror("Failed to invoke sendmail");
        }
    }

    void process(LocalFeed &feed) {
        printf("[%s]: ", feed.title().value().c_str()); fflush(stdout);

        // TODO: Store and check etags/skipdays etc
        mailrss::HTTPRequest request(feed.URL().value());
        request.perform();
        if (request.error.length() > 0) {
            printf("skipping because request failed: %s\n", request.error.c_str());
            return;
        }

        tinyxml2::XMLDocument feedDocument;
        feedDocument.Parse(request.result.c_str());
        auto remoteFeed = Feed::parseDocument(feedDocument);
        if (!remoteFeed) {
            puts("skipping because it isn't an RSS feed or was invalid.");
            return;
        }

        auto unseenEntries = feed.unseenEntriesInRemoteFeed(remoteFeed.value());
        if (unseenEntries.size() > 0) {
            printf("sending email for %lu new posts...", unseenEntries.size());
        } else {
            puts("nothing new.");
            return;
        }

        mailrss::Feed::Entry *lastSentEntry = nullptr;
        for (auto entryIterator = unseenEntries.rbegin(); entryIterator != unseenEntries.rend(); ++entryIterator) {
            auto mail = mailrss::formatEmail(remoteFeed.value(), *entryIterator);
            // Send here
            bool success = true;
            if (success)
                lastSentEntry = &*entryIterator;
        }

        if (lastSentEntry != nullptr) {
            feed.setLastSeenEntryGUID(lastSentEntry->GUID().value());
        }
        puts("\tdone.");
    }

    class LocalFeedManager {
    public:
        vector<LocalFeed> feeds;
        LocalFeedManager(string feedDocumentName = "feeds.opml"): feedDocumentName(feedDocumentName) {
            feedDocument.LoadFile(feedDocumentName.c_str());
            mailrss::OPMLParser parser;
            feedDocument.Accept(&parser);
            feeds = parser.feeds;
        }

        void listFeeds() {
            for (size_t index = 0; index < feeds.size(); ++index) {
                auto feed = feeds[index];
                printf("%2i: %-50s%s\n", (int)index, feed.title().value().c_str(), feed.URL().value().c_str());
            }
        }

        void deleteFeed(int index) {
            if (feeds.size() > index) {
                auto feed = feeds[index];
                feeds.erase(feeds.begin() + index);
                printf("Deleting %s\n", feed.title().value().c_str());
                feed.remove();
                feedDocument.SaveFile(feedDocumentName.c_str());
            } else {
                printf("No feed at index %i\n", index);
            }
        }

        void processFeeds() {
            for (auto feed : feeds) {
                mailrss::process(feed);
                feedDocument.SaveFile(feedDocumentName.c_str());
            }
        }
    private:
        string feedDocumentName;
        tinyxml2::XMLDocument feedDocument;
    };
}

int main(int argc, char *argv[]) {
    vector<string> arguments(argv + 1, argv + argc);
    mailrss::LocalFeedManager feedManager;

    if (arguments.size() == 0) {
        feedManager.processFeeds();
        return EXIT_SUCCESS;
    }

    if (find(arguments.begin(), arguments.end(), "list") != arguments.end()) {
        feedManager.listFeeds();
        return EXIT_SUCCESS;
    }

    auto deleteIterIndex = find(arguments.begin(), arguments.end(), "delete");
    if (deleteIterIndex != arguments.end()) {
        try {
            if (++deleteIterIndex == arguments.end())
                throw std::out_of_range("not enough arguments for delete");
            int index = stoi(*deleteIterIndex);
            if (index >= feedManager.feeds.size())
                throw std::out_of_range("specified feed that doesn't exist for delete");
            feedManager.deleteFeed(index);
            return EXIT_SUCCESS;
        } catch (const std::exception& e) {
            puts("Usage: 'mailrss delete N' where N must be an index taken from 'mailrss list'");
            return EXIT_FAILURE;
        }
    }

    puts("Available commands: list, delete, add, dry-run");
    return EXIT_FAILURE;
}