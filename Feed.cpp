#include "Feed.hpp"
#include <sstream>

namespace mailrss {

    optional<string> XMLElementWrapper::textOfChildElement(string childElementName) const {
        auto childElement = element->FirstChildElement(childElementName.c_str());
        if (childElement) {
            const char *text = childElement->GetText();
            if (text) return text;
        }
        return {};
    }

    optional<string> XMLElementWrapper::textOfAttribute(string attributeName) {
        auto attributeText = element->Attribute(attributeName.c_str());
        if (attributeText) {
            return attributeText;
        }
        return {};
    }

    void XMLElementWrapper::setTextOfAttribute(string attributeName, string text) {
        element->SetAttribute(attributeName.c_str(), text.c_str());
    }

    class RSSFeed: public Feed {
    private:
        class RSSEntry: public Entry {
        public:
            RSSEntry(tinyxml2::XMLElement *element): Entry(element) {}
            optional<string> title() const { return textOfChildElement("title"); }
            optional<string> URL() const { return textOfChildElement("link"); }
            optional<string> description() const { return textOfChildElement("description"); }
            optional<string> GUID() const {
                // Try different fields in order of preference since none is required
                auto guid = textOfChildElement("guid");
                if (guid) return guid;
                if (URL()) return URL();
                if (title()) return title();
                if (description()) {
                    // Hash the content of description as a last resort
                    std::hash<string> hasher;
                    size_t hash = hasher(description().value());
                    std::ostringstream stringStream;
                    stringStream << hash;
                    string a;
                    return stringStream.str();
                }
                return {};
            }

            bool hasHTMLContent() const {
                return false;
            }

            ~RSSEntry() {}
        };

    public:
        RSSFeed(tinyxml2::XMLElement *channelElement): Feed(channelElement) {
            auto item = channelElement->FirstChildElement("item");
            for (; item != nullptr; item = item->NextSiblingElement("item")) {
                entries.push_back(std::make_unique<RSSEntry>(RSSEntry(item)));
            }
        }
    };

    class AtomFeed: public Feed {
    private:
        class AtomEntry: public Entry {
        public:
            AtomEntry(tinyxml2::XMLElement *element): Entry(element) {}
            optional<string> title() const { return textOfChildElement("title"); }
            optional<string> GUID() const { return textOfChildElement("id"); }
            optional<string> URL() const {
                auto link = element->FirstChildElement("link");

                // Enumerate link elements and check the relation ("rel"-attribute).
                // If there is no relation we assume it's the link we want, otherwise we look
                // for the "alternate" attribute
                for (; link != nullptr; link = link->NextSiblingElement("link")) {
                    auto relation = link->Attribute("rel");
                    string alternate = "alternate";
                    if (relation == nullptr || alternate == relation) {
                        return link->Attribute("href");
                    }
                }
                return {};
            }

            optional<string> description() const {
                auto content = textOfChildElement("content");
                if (content) {
                    return content;
                }
                return textOfChildElement("summary");
            }

            bool hasHTMLContent() const {
                auto contentElement = element->FirstChildElement("content");
                if (!contentElement) contentElement = element->FirstChildElement("summary");
                if (!contentElement) return false;
                auto type = contentElement->Attribute("type");
                if (type) {
                    return string("html") == type || string("xhtml") == type;
                }
                return false;
            }

            ~AtomEntry() {}
        };

    public:
        AtomFeed(tinyxml2::XMLElement *channelElement): Feed(channelElement) {
            auto item = channelElement->FirstChildElement("entry");
            for (; item != nullptr; item = item->NextSiblingElement("entry")) {
                entries.push_back(std::make_unique<AtomEntry>(AtomEntry(item)));
            }
        }
    };


    optional<Feed> Feed::parseDocument(tinyxml2::XMLDocument &document) {
        auto rss = document.FirstChildElement("rss");
        if (rss) {
            auto channelElement = rss->FirstChildElement("channel");
            return RSSFeed(channelElement);
        }

        // Atom
        auto feedElement = document.FirstChildElement("feed");
        if (feedElement) {
            auto namespaceAttribute = feedElement->Attribute("xmlns");
            string expectedNamespace = "http://www.w3.org/2005/Atom";
            if (namespaceAttribute && expectedNamespace == namespaceAttribute) {
                return AtomFeed(feedElement);
            }
        }
        return {};
    }
}
