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
#include <stdlib.h>
#include "Feed.hpp"

namespace mailrss {

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

        std::vector<Feed::Entry *> unseenEntriesInRemoteFeed(Feed &feed) {
            std::vector<Feed::Entry *> unseenEntries;
            for (auto &entry : feed.entries) {
                if (entry->GUID() == lastSeenEntryGUID())
                    return unseenEntries;
                unseenEntries.push_back(entry.get());
            }
            return unseenEntries;
        }
    };

    class OPMLParser : public tinyxml2::XMLVisitor {
    public:
        std::vector<LocalFeed> feeds;

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
        std::vector<char> errorBuffer = std::vector<char>(CURL_ERROR_SIZE);
        string URL;

        static int curlWriter(char *data, size_t size, size_t nmemb, string *writerData) {
            if(writerData == NULL)
                return 0;
            writerData->append(data, size * nmemb);
            return (int)size * (int)nmemb;
        }
    };

    void replaceWord(string &text, string word, optional<string> replacement) {
        if (!replacement)
            return;

        size_t index;
        while ((index = text.find(word)) != string::npos) {
            text.replace(index, word.length(), *replacement);
        }
    }

    string formatEmail(LocalFeed& feed, const Feed::Entry *entry) {
        std::ifstream templateFile("template.mail");
        std::stringstream buffer;
        buffer << templateFile.rdbuf();
        auto text = buffer.str();
        replaceWord(text, "{{feed_title}}", feed.title());
        replaceWord(text, "{{article_title}}", entry->title());
        replaceWord(text, "{{article_content}}", entry->content());
        replaceWord(text, "{{article_url}}", entry->URL());
        replaceWord(text, "{{article_content_type}}", entry->hasHTMLContent() ? "text/html; charset=\"UTF-8\"" : "text/plain");
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

    void process(LocalFeed &feed, bool sendEmails = false) {
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
            printf("%s %lu new posts...", sendEmails ? "sending email for" : "synced", (long)unseenEntries.size()); fflush(stdout);
        } else {
            puts("nothing new.");
            return;
        }

        mailrss::Feed::Entry *lastSentEntry = nullptr;
        for (auto entryIterator = unseenEntries.rbegin(); entryIterator != unseenEntries.rend(); ++entryIterator) {
            if (sendEmails) {
                auto mail = mailrss::formatEmail(feed, *entryIterator);
                sendmail(mail);
            }
            lastSentEntry = *entryIterator;
        }

        if (lastSentEntry != nullptr) {
            feed.setLastSeenEntryGUID(lastSentEntry->GUID().value());
        }
        puts("\tdone.");
    }

    class LocalFeedManager {
    public:
        std::vector<LocalFeed> feeds;
        tinyxml2::XMLDocument feedDocument;
        string feedDocumentName;

        LocalFeedManager(string feedDocumentName): feedDocumentName(feedDocumentName) {
            tinyxml2::XMLError error = feedDocument.LoadFile(feedDocumentName.c_str());
            if (error == tinyxml2::XML_ERROR_FILE_NOT_FOUND) {
                throw std::invalid_argument("Unable to find " + feedDocumentName);
            } else if (error) {
                throw std::domain_error(feedDocumentName + " could not be opened. Make sure the file has the correct permissions.");
            }

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

        void deleteFeed(unsigned int index) {
            if (feeds.size() > index) {
                auto feed = feeds[index];
                feeds.erase(feeds.begin() + index);
                printf("Deleting %s\n", feed.title().value().c_str());
                feed.remove();
                feedDocument.SaveFile(feedDocumentName.c_str());
            } else {
                throw std::out_of_range("Feed " + std::to_string(index) + " out of bounds. (0-" + std::to_string(feeds.size()) + ")");
            }
        }

        // Processes all feeds and stores state, but skips sending e-mails
        void syncFeeds() {
            for (auto feed : feeds) {
                mailrss::process(feed, false);
                feedDocument.SaveFile(feedDocumentName.c_str());
            }
        }

        // Processes all feeds and sends e-mails for unseed entries
        void processFeeds() {
            for (auto feed : feeds) {
                mailrss::process(feed, true);
                feedDocument.SaveFile(feedDocumentName.c_str());
            }
        }
    };

    class Command {
    public:
        string name;
        optional<int> index;
        optional<string> file;

        static Command parseOptions(int argc, char *argv[]) {
            std::vector<string> arguments(argv + 1, argv + argc);
            auto argumentIterator = arguments.begin();
            if (argumentIterator == arguments.end()) {
                throw std::invalid_argument("missing command");
            }

            string command = *argumentIterator++;
            std::vector<string> availableCommands = { "sync", "run", "list", "delete", "add" };
            if (find(availableCommands.begin(), availableCommands.end(), command) == availableCommands.end()) {
                throw std::invalid_argument(command + " is not a valid command");
            }

            optional<int> index = {};
            if (command == "delete") {
                if (argumentIterator == arguments.end())
                    throw std::out_of_range("Not enough arguments for delete. Usage: 'mailrss delete [index] [[file]]' where index is the number of the feed in 'mailrss list'");
                try {
                    index = stoi(*argumentIterator++);
                } catch (std::exception) {
                    throw std::invalid_argument("'delete' requires an integer argument");
                }
            }

            optional<string> file = {};
            if (argumentIterator != arguments.end()) {
                file = *argumentIterator;
            }

            return Command(command, index, file);
        }
    private:
        Command(string name, optional<int> index, optional<string> file): name(name), index(index), file(file) {}
    };
}

void printHelp() {
    puts("Available commands: sync, run, list, delete, add");
    puts("Usage: mailrss [command] [[arg]] [[file]]");
}

int main(int argc, char *argv[]) {
    try {
        auto command = mailrss::Command::parseOptions(argc, argv);
        string fileName = command.file ? command.file.value() : std::string(getenv("HOME")) + "/.mailrss.opml";
        mailrss::LocalFeedManager feedManager(fileName);
        if (command.name == "sync") {
            feedManager.syncFeeds();
        } else if (command.name == "run") {
            feedManager.processFeeds();
        } else if (command.name == "list") {
            feedManager.listFeeds();
        } else if (command.name == "delete") {
            feedManager.deleteFeed(command.index.value());
        }
    } catch (std::exception& e) {
        puts(e.what());
        printHelp();
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
