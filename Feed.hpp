#pragma once

#include <string>
#include <vector>
#include <memory>
#include "lib/tinyxml2/tinyxml2.h"

#if (defined(__GNUC__) && !defined(__clang__))
#include <experimental/optional>
using std::experimental::optional;
#else
#include <optional>
using std::optional;
#endif

using std::string;

namespace mailrss {
    class XMLElementWrapper {
    public:
        XMLElementWrapper(tinyxml2::XMLElement *element): element(element) {}
        optional<string> textOfChildElement(string childElementName) const;
        optional<string> textOfAttribute(string attributeName);
        void setTextOfAttribute(string attributeName, string text);
    protected:
        tinyxml2::XMLElement *element;
    };

    class Feed: public XMLElementWrapper {
    public:
        class Entry: public XMLElementWrapper {
        public:
            virtual optional<string> title() const = 0;
            virtual optional<string> URL() const = 0;
            virtual optional<string> description() const = 0;
            virtual optional<string> GUID() const = 0;
            virtual bool hasHTMLContent() const = 0;
            virtual ~Entry() {}
        protected:
            Entry(tinyxml2::XMLElement *element): XMLElementWrapper(element) {}
        };
        std::vector<std::unique_ptr<Entry>> entries;
        static optional<Feed> parseDocument(tinyxml2::XMLDocument &document);
    protected:
        Feed(tinyxml2::XMLElement *element): XMLElementWrapper(element) {}
    };
}
