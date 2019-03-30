#include "Feed.hpp"
#include <sstream>
#include <memory>

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

    optional<Feed> Feed::parseDocument(tinyxml2::XMLDocument &document) {
        auto rss = document.FirstChildElement("rss");
        if (rss) {
            auto channelElement = rss->FirstChildElement("channel");
            return RSSFeed(channelElement);
        }
        return {};
    }
}
