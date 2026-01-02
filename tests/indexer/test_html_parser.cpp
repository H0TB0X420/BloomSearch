#include "indexer/html_parser.h"
#include <iostream>
#include <cassert>

using namespace search;

struct TestResults {
    int passed = 0;
    int failed = 0;
};

void print_test(bool passed, const std::string& name, TestResults& results) {
    if (passed) {
        std::cout << "[PASS] " << name << "\n";
        results.passed++;
    } else {
        std::cout << "[FAIL] " << name << "\n";
        results.failed++;
    }
}

//=============================================================================
// Test 1: Title extraction
//=============================================================================
void test_title_extraction(TestResults& results) {
    std::cout << "\n--- Test: Title Extraction ---\n";
    
    HTMLParser parser;
    
    // Basic title
    std::string html1 = "<html><head><title>Hello World</title></head><body></body></html>";
    print_test(parser.extract_title(html1) == "Hello World", "Basic title", results);
    
    // Title with whitespace
    std::string html2 = "<html><head><title>  Trimmed Title  </title></head></html>";
    print_test(parser.extract_title(html2) == "Trimmed Title", "Title with whitespace trimmed", results);
    
    // Missing title
    std::string html3 = "<html><head></head><body>No title</body></html>";
    print_test(parser.extract_title(html3).empty(), "Missing title returns empty", results);
    
    // Title with special chars
    std::string html4 = "<html><head><title>Test &amp; Title</title></head></html>";
    std::string title4 = parser.extract_title(html4);
    print_test(title4.find("Test") != std::string::npos, "Title with entities", results);
}

//=============================================================================
// Test 2: Text extraction
//=============================================================================
void test_text_extraction(TestResults& results) {
    std::cout << "\n--- Test: Text Extraction ---\n";
    
    HTMLParser parser;
    
    // Basic text
    std::string html1 = "<html><body><p>Hello World</p></body></html>";
    std::string text1 = parser.extract_text(html1);
    print_test(text1.find("Hello World") != std::string::npos, "Basic paragraph text", results);
    
    // Skip script tags
    std::string html2 = "<html><body><script>var x = 1;</script><p>Visible</p></body></html>";
    std::string text2 = parser.extract_text(html2);
    print_test(text2.find("var x") == std::string::npos, "Script content skipped", results);
    print_test(text2.find("Visible") != std::string::npos, "Visible text preserved", results);
    
    // Skip style tags
    std::string html3 = "<html><body><style>.foo { color: red; }</style><p>Content</p></body></html>";
    std::string text3 = parser.extract_text(html3);
    print_test(text3.find("color") == std::string::npos, "Style content skipped", results);
    print_test(text3.find("Content") != std::string::npos, "Content after style preserved", results);
    
    // Multiple paragraphs
    std::string html4 = "<html><body><p>First</p><p>Second</p></body></html>";
    std::string text4 = parser.extract_text(html4);
    print_test(text4.find("First") != std::string::npos && 
               text4.find("Second") != std::string::npos, "Multiple paragraphs", results);
    
    // Nested elements
    std::string html5 = "<html><body><div><span>Nested</span> text</div></body></html>";
    std::string text5 = parser.extract_text(html5);
    print_test(text5.find("Nested") != std::string::npos && 
               text5.find("text") != std::string::npos, "Nested elements", results);
}

//=============================================================================
// Test 3: Meta tag extraction
//=============================================================================
void test_meta_extraction(TestResults& results) {
    std::cout << "\n--- Test: Meta Tag Extraction ---\n";
    
    HTMLParser parser;
    
    std::string html = R"(
        <html>
        <head>
            <meta name="description" content="A test page">
            <meta name="keywords" content="test, html, parser">
            <meta name="author" content="Test Author">
            <meta property="article:published_time" content="2024-01-15T10:00:00Z">
            <meta property="og:title" content="OG Title">
        </head>
        <body></body>
        </html>
    )";
    
    auto meta = parser.extract_meta_tags(html);
    
    print_test(meta["description"] == "A test page", "Description meta", results);
    print_test(meta["keywords"] == "test, html, parser", "Keywords meta", results);
    print_test(meta["author"] == "Test Author", "Author meta", results);
    print_test(meta["article:published_time"] == "2024-01-15T10:00:00Z", "Published time meta", results);
    print_test(meta["og:title"] == "OG Title", "OpenGraph meta", results);
}

//=============================================================================
// Test 4: Link extraction
//=============================================================================
void test_link_extraction(TestResults& results) {
    std::cout << "\n--- Test: Link Extraction ---\n";
    
    HTMLParser parser;
    
    std::string html = R"-(
        <html><body>
            <a href="https://example.com">Example</a>
            <a href="/relative/path">Relative Link</a>
            <a href="#anchor">Skip Me</a>
            <a href="javascript:void(0)">Skip JS</a>
            <a href="page.html">Page Link</a>
        </body></html>)-";
    
    auto links = parser.extract_links(html, "https://base.com/dir/");
    
    print_test(links.size() >= 3, "Found at least 3 valid links", results);
    
    // Check absolute URL preserved
    bool found_example = false;
    for (const auto& link : links) {
        if (link.url == "https://example.com" && link.anchor_text == "Example") {
            found_example = true;
            break;
        }
    }
    print_test(found_example, "Absolute URL with anchor text", results);
    
    // Check relative URL resolved
    bool found_relative = false;
    for (const auto& link : links) {
        if (link.url == "https://base.com/relative/path") {
            found_relative = true;
            break;
        }
    }
    print_test(found_relative, "Relative URL resolved", results);
    
    // Check hash links skipped
    bool found_anchor = false;
    for (const auto& link : links) {
        if (link.url.find("#anchor") != std::string::npos) {
            found_anchor = true;
            break;
        }
    }
    print_test(!found_anchor, "Hash links skipped", results);
}

//=============================================================================
// Test 5: Full document parsing
//=============================================================================
void test_full_parsing(TestResults& results) {
    std::cout << "\n--- Test: Full Document Parsing ---\n";
    
    HTMLParser parser;
    
    std::string html = R"(
        <!DOCTYPE html>
        <html>
        <head>
            <title>Test Page Title</title>
            <meta name="description" content="Test description">
            <meta name="keywords" content="test, keywords">
        </head>
        <body>
            <nav><a href="/home">Home</a></nav>
            <main>
                <article>
                    <h1>Main Heading</h1>
                    <p>This is the main content of the page.</p>
                    <p>It has multiple paragraphs with <a href="https://link.com">links</a>.</p>
                </article>
            </main>
            <footer>Copyright 2024</footer>
            <script>console.log("hidden");</script>
        </body>
        </html>
    )";
    
    auto doc = parser.parse(html, "https://example.com/page/");
    
    print_test(doc.title == "Test Page Title", "Document title extracted", results);
    print_test(doc.description == "Test description", "Document description extracted", results);
    print_test(doc.keywords == "test, keywords", "Document keywords extracted", results);
    print_test(doc.text_content.find("Main Heading") != std::string::npos, "Heading in text", results);
    print_test(doc.text_content.find("main content") != std::string::npos, "Content in text", results);
    print_test(doc.text_content.find("console.log") == std::string::npos, "Script not in text", results);
    print_test(doc.link_count >= 2, "Links counted", results);
    print_test(doc.word_count > 10, "Words counted", results);
    
    std::cout << "[INFO] Word count: " << doc.word_count << "\n";
    std::cout << "[INFO] Link count: " << doc.link_count << "\n";
}

//=============================================================================
// Test 6: Malformed HTML handling
//=============================================================================
void test_malformed_html(TestResults& results) {
    std::cout << "\n--- Test: Malformed HTML ---\n";
    
    HTMLParser parser;
    
    // Unclosed tags
    std::string html1 = "<html><body><p>Unclosed paragraph<div>More text</body>";
    std::string text1 = parser.extract_text(html1);
    print_test(text1.find("Unclosed") != std::string::npos, "Handles unclosed tags", results);
    
    // Missing html/body
    std::string html2 = "<p>Just a paragraph</p>";
    std::string text2 = parser.extract_text(html2);
    print_test(text2.find("Just a paragraph") != std::string::npos, "Handles missing structure", results);
    
    // Empty input
    std::string text3 = parser.extract_text("");
    print_test(text3.empty(), "Empty input returns empty", results);
}

int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    HTML Parser Test Suite - Task 2.1                       \n";
    std::cout << "============================================================\n";
    
    TestResults results;
    
    test_title_extraction(results);
    test_text_extraction(results);
    test_meta_extraction(results);
    test_link_extraction(results);
    test_full_parsing(results);
    test_malformed_html(results);
    
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed: " << results.passed << "\n";
    std::cout << "Failed: " << results.failed << "\n";
    std::cout << "\n";
    
    if (results.failed == 0) {
        std::cout << "[SUCCESS] All HTML parser tests passed!\n\n";
        return 0;
    } else {
        std::cout << "[FAILED] Some tests failed\n\n";
        return 1;
    }
}