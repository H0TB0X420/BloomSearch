#include "indexer/html_parser.h"
#include <gumbo.h>
#include <algorithm>
#include <cctype>
#include <sstream>

namespace search {

ParsedDocument HTMLParser::parse(const std::string& html, const std::string& base_url) {
    ParsedDocument doc;
    
    if (html.empty()) {
        last_error_ = "Empty HTML";
        return doc;
    }
    
    GumboOutput* output = gumbo_parse(html.c_str());
    if (!output) {
        last_error_ = "Gumbo parsing failed";
        return doc;
    }
    
    doc.title = extract_title(html);
    doc.text_content = extract_text(html);
    doc.links = extract_links(html, base_url);
    
    auto meta = extract_meta_tags(html);
    doc.description = meta["description"];
    doc.keywords = meta["keywords"];
    doc.author = meta["author"];
    
    if (meta.count("article:published_time")) {
        doc.published_date = meta["article:published_time"];
    } else if (meta.count("date")) {
        doc.published_date = meta["date"];
    } else if (meta.count("pubdate")) {
        doc.published_date = meta["pubdate"];
    }
    
    if (meta.count("article:modified_time")) {
        doc.modified_date = meta["article:modified_time"];
    } else if (meta.count("last-modified")) {
        doc.modified_date = meta["last-modified"];
    }
    
    doc.link_count = doc.links.size();
    
    std::istringstream iss(doc.text_content);
    std::string word;
    while (iss >> word) {
        doc.word_count++;
    }
    
    gumbo_destroy_output(&kGumboDefaultOptions, output);
    
    return doc;
}

std::string HTMLParser::extract_title(const std::string& html) {
    GumboOutput* output = gumbo_parse(html.c_str());
    if (!output) return "";
    
    std::string title;
    
    GumboNode* root = output->root;
    if (root->type == GUMBO_NODE_ELEMENT) {
        const GumboVector* children = &root->v.element.children;
        for (unsigned int i = 0; i < children->length; ++i) {
            GumboNode* child = static_cast<GumboNode*>(children->data[i]);
            if (child->type == GUMBO_NODE_ELEMENT && 
                child->v.element.tag == GUMBO_TAG_HEAD) {
                const GumboVector* head_children = &child->v.element.children;
                for (unsigned int j = 0; j < head_children->length; ++j) {
                    GumboNode* head_child = static_cast<GumboNode*>(head_children->data[j]);
                    if (head_child->type == GUMBO_NODE_ELEMENT &&
                        head_child->v.element.tag == GUMBO_TAG_TITLE) {
                        const GumboVector* title_children = &head_child->v.element.children;
                        for (unsigned int k = 0; k < title_children->length; ++k) {
                            GumboNode* text_node = static_cast<GumboNode*>(title_children->data[k]);
                            if (text_node->type == GUMBO_NODE_TEXT) {
                                title = text_node->v.text.text;
                                break;
                            }
                        }
                        break;
                    }
                }
                break;
            }
        }
    }
    
    gumbo_destroy_output(&kGumboDefaultOptions, output);
    return trim_whitespace(title);
}

std::string HTMLParser::extract_text(const std::string& html) {
    GumboOutput* output = gumbo_parse(html.c_str());
    if (!output) return "";
    
    std::string text;
    bool in_main = false;
    extract_text_recursive(output->root, text, in_main);
    
    gumbo_destroy_output(&kGumboDefaultOptions, output);
    
    std::string result;
    result.reserve(text.size());
    bool last_was_space = true;
    
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!last_was_space) {
                result += ' ';
                last_was_space = true;
            }
        } else {
            result += c;
            last_was_space = false;
        }
    }
    
    while (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    
    return result;
}

void HTMLParser::extract_text_recursive(const GumboNode* node, 
                                        std::string& output,
                                        bool& in_main_content) {
    if (node->type == GUMBO_NODE_TEXT) {
        output += node->v.text.text;
        output += ' ';
        return;
    }
    
    if (node->type != GUMBO_NODE_ELEMENT) {
        return;
    }
    
    if (is_invisible_tag(node->v.element.tag)) {
        return;
    }
    
    if (node->v.element.tag == GUMBO_TAG_MAIN ||
        node->v.element.tag == GUMBO_TAG_ARTICLE) {
        in_main_content = true;
    }
    
    const GumboVector* children = &node->v.element.children;
    for (unsigned int i = 0; i < children->length; ++i) {
        extract_text_recursive(static_cast<GumboNode*>(children->data[i]), 
                              output, in_main_content);
    }
    
    GumboTag tag = node->v.element.tag;
    if (tag == GUMBO_TAG_P || tag == GUMBO_TAG_DIV || 
        tag == GUMBO_TAG_BR || tag == GUMBO_TAG_LI ||
        tag == GUMBO_TAG_H1 || tag == GUMBO_TAG_H2 ||
        tag == GUMBO_TAG_H3 || tag == GUMBO_TAG_H4 ||
        tag == GUMBO_TAG_H5 || tag == GUMBO_TAG_H6) {
        output += '\n';
    }
}

bool HTMLParser::is_invisible_tag(int tag) const {
    GumboTag t = static_cast<GumboTag>(tag);
    return t == GUMBO_TAG_SCRIPT ||
           t == GUMBO_TAG_STYLE ||
           t == GUMBO_TAG_NOSCRIPT ||
           t == GUMBO_TAG_TEMPLATE ||
           t == GUMBO_TAG_HEAD ||
           t == GUMBO_TAG_META ||
           t == GUMBO_TAG_LINK;
}

bool HTMLParser::is_boilerplate_tag(int tag) const {
    GumboTag t = static_cast<GumboTag>(tag);
    return t == GUMBO_TAG_NAV ||
           t == GUMBO_TAG_FOOTER ||
           t == GUMBO_TAG_HEADER ||
           t == GUMBO_TAG_ASIDE;
}

std::vector<ExtractedLink> HTMLParser::extract_links(const std::string& html, 
                                                      const std::string& base_url) {
    std::vector<ExtractedLink> links;
    
    GumboOutput* output = gumbo_parse(html.c_str());
    if (!output) return links;
    
    extract_links_recursive(output->root, links, base_url);
    
    gumbo_destroy_output(&kGumboDefaultOptions, output);
    return links;
}

void HTMLParser::extract_links_recursive(const GumboNode* node,
                                         std::vector<ExtractedLink>& links,
                                         const std::string& base_url) {
    if (node->type != GUMBO_NODE_ELEMENT) {
        return;
    }
    
    if (node->v.element.tag == GUMBO_TAG_A) {
        std::string href = get_attribute(node, "href");
        
        if (!href.empty() && href[0] != '#' &&
            href.find("javascript:") != 0 &&
            href.find("mailto:") != 0) {
            
            ExtractedLink link;
            link.url = normalize_url(href, base_url);
            
            std::string anchor;
            bool dummy = false;
            extract_text_recursive(node, anchor, dummy);
            link.anchor_text = trim_whitespace(anchor);
            
            if (!link.url.empty()) {
                links.push_back(std::move(link));
            }
        }
    }
    
    const GumboVector* children = &node->v.element.children;
    for (unsigned int i = 0; i < children->length; ++i) {
        extract_links_recursive(static_cast<GumboNode*>(children->data[i]), 
                               links, base_url);
    }
}

std::unordered_map<std::string, std::string> HTMLParser::extract_meta_tags(const std::string& html) {
    std::unordered_map<std::string, std::string> meta;
    
    GumboOutput* output = gumbo_parse(html.c_str());
    if (!output) return meta;
    
    GumboNode* root = output->root;
    if (root->type != GUMBO_NODE_ELEMENT) {
        gumbo_destroy_output(&kGumboDefaultOptions, output);
        return meta;
    }
    
    const GumboVector* children = &root->v.element.children;
    for (unsigned int i = 0; i < children->length; ++i) {
        GumboNode* child = static_cast<GumboNode*>(children->data[i]);
        if (child->type == GUMBO_NODE_ELEMENT && 
            child->v.element.tag == GUMBO_TAG_HEAD) {
            
            const GumboVector* head_children = &child->v.element.children;
            for (unsigned int j = 0; j < head_children->length; ++j) {
                GumboNode* meta_node = static_cast<GumboNode*>(head_children->data[j]);
                
                if (meta_node->type == GUMBO_NODE_ELEMENT &&
                    meta_node->v.element.tag == GUMBO_TAG_META) {
                    
                    std::string name = get_attribute(meta_node, "name");
                    std::string property = get_attribute(meta_node, "property");
                    std::string content = get_attribute(meta_node, "content");
                    
                    if (!name.empty() && !content.empty()) {
                        std::transform(name.begin(), name.end(), name.begin(),
                                      [](unsigned char c) { return std::tolower(c); });
                        meta[name] = content;
                    }
                    
                    if (!property.empty() && !content.empty()) {
                        std::transform(property.begin(), property.end(), property.begin(),
                                      [](unsigned char c) { return std::tolower(c); });
                        meta[property] = content;
                    }
                }
            }
            break;
        }
    }
    
    gumbo_destroy_output(&kGumboDefaultOptions, output);
    return meta;
}

std::string HTMLParser::get_attribute(const GumboNode* node, const char* name) {
    if (node->type != GUMBO_NODE_ELEMENT) {
        return "";
    }
    
    GumboAttribute* attr = gumbo_get_attribute(&node->v.element.attributes, name);
    if (attr) {
        return attr->value;
    }
    return "";
}

std::string HTMLParser::normalize_url(const std::string& url, const std::string& base_url) {
    if (url.find("http://") == 0 || url.find("https://") == 0) {
        return url;
    }
    
    if (base_url.empty()) {
        return url;
    }
    
    auto scheme_end = base_url.find("://");
    if (scheme_end == std::string::npos) return "";
    
    std::string base_scheme = base_url.substr(0, scheme_end);
    size_t host_start = scheme_end + 3;
    size_t path_start = base_url.find('/', host_start);
    
    std::string base_host;
    if (path_start == std::string::npos) {
        base_host = base_url.substr(host_start);
    } else {
        base_host = base_url.substr(host_start, path_start - host_start);
    }
    
    if (url.find("//") == 0) {
        return base_scheme + ":" + url;
    }
    
    if (!url.empty() && url[0] == '/') {
        return base_scheme + "://" + base_host + url;
    }
    
    std::string base_path = (path_start != std::string::npos) 
                            ? base_url.substr(path_start) 
                            : "/";
    size_t last_slash = base_path.rfind('/');
    std::string base_dir = (last_slash != std::string::npos) 
                           ? base_path.substr(0, last_slash + 1) 
                           : "/";
    
    return base_scheme + "://" + base_host + base_dir + url;
}

std::string HTMLParser::trim_whitespace(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r\f\v");
    if (start == std::string::npos) return "";
    
    size_t end = str.find_last_not_of(" \t\n\r\f\v");
    return str.substr(start, end - start + 1);
}

} // namespace search