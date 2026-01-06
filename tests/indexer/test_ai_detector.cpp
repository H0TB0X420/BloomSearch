#include "indexer/ai_detector.h"
#include <iostream>
#include <iomanip>

using namespace search;

struct TestResults {
    int passed = 0;
    int failed = 0;
};

void print_test(bool passed, const std::string& name, TestResults& results) {
    std::cout << (passed ? "[PASS] " : "[FAIL] ") << name << "\n";
    passed ? results.passed++ : results.failed++;
}

void test_human_text(TestResults& results) {
    std::cout << "\n--- Test: Human-Written Text ---\n";
    
    AIDetector detector;
    
    // Real human blog post style - varied sentence length, natural flow
    std::string human_text = R"(
I woke up this morning thinking about coffee. Not in a philosophical way, 
just—I really wanted some. The machine was broken again.

So I walked to the cafe down the street. It's one of those places where 
the barista remembers your order. Small town stuff. 

The coffee was good. Really good, actually. Sat there for an hour reading 
the paper. An actual paper newspaper! They still print those, apparently.

Back home now. Machine's still broken. Maybe I'll fix it tomorrow. Or maybe 
I'll just keep walking to the cafe. The exercise is probably good for me anyway.

Who knows. Life's weird like that sometimes.
    )";
    
    float score = detector.calculate_score(human_text);
    std::cout << "  Human text score: " << std::fixed << std::setprecision(3) << score << "\n";
    print_test(score < 0.4f, "Human text scores low (<0.4)", results);
    
    // Technical human writing
    std::string tech_human = R"(
The bug was in the parser. Took me three hours to find it—a single off-by-one 
error in the tokenizer loop. Classic.

Here's what happened: when the input string ended with whitespace, the loop 
counter would increment past the buffer. Boom. Segfault.

The fix was simple once I found it. Added a bounds check. Two lines of code.

I should probably write a test for this. Actually, I should have written 
the test first. We all say that. Nobody does it.

Anyway, pushing the fix now. PR #4521 if anyone wants to review.
    )";
    
    float tech_score = detector.calculate_score(tech_human);
    std::cout << "  Tech human score: " << std::fixed << std::setprecision(3) << tech_score << "\n";
    print_test(tech_score < 0.4f, "Technical human text scores low", results);
}

void test_ai_text(TestResults& results) {
    std::cout << "\n--- Test: AI-Generated Text ---\n";
    
    AIDetector detector;
    
    // Classic AI-generated article style
    std::string ai_text = R"(
In today's rapidly evolving digital landscape, it's important to note that 
artificial intelligence has become a crucial component of modern technology. 
Let's delve into the multifaceted world of AI and explore its myriad applications.

Furthermore, the comprehensive nature of AI systems allows for seamless 
integration across various platforms. Additionally, these robust solutions 
leverage cutting-edge algorithms to enhance user experiences.

Moreover, it's essential to understand that AI plays a pivotal role in 
streamlining business operations. Consequently, organizations are increasingly 
utilizing these innovative tools to foster growth and facilitate success.

In conclusion, the tapestry of artificial intelligence continues to expand, 
offering unprecedented opportunities for those who embrace its potential. 
At the end of the day, AI represents a paradigm shift in how we approach 
complex challenges in the modern era.
    )";
    
    float score = detector.calculate_score(ai_text);
    std::cout << "  AI text score: " << std::fixed << std::setprecision(3) << score << "\n";
    print_test(score > 0.5f, "AI text scores high (>0.5)", results);
    
    // Another AI pattern - listicle style
    std::string ai_listicle = R"(
Welcome to this comprehensive guide on productivity tips! In this article, 
we will explore ten essential strategies to enhance your daily workflow.

Firstly, it's crucial to establish a morning routine. This foundational habit 
sets the tone for a productive day ahead.

Secondly, leveraging digital tools can significantly streamline your tasks. 
Modern applications offer robust features for optimal time management.

Thirdly, taking regular breaks is vital for maintaining focus. Studies show 
that brief pauses enhance overall cognitive performance.

Additionally, creating a dedicated workspace fosters concentration. A clutter-free 
environment plays a pivotal role in productivity.

Furthermore, setting clear goals provides direction and motivation. Specific 
objectives help prioritize tasks effectively.

In summary, implementing these strategies can transform your productivity 
journey. Remember, consistency is key to achieving lasting results.
    )";
    
    float listicle_score = detector.calculate_score(ai_listicle);
    std::cout << "  AI listicle score: " << std::fixed << std::setprecision(3) << listicle_score << "\n";
    print_test(listicle_score > 0.5f, "AI listicle scores high", results);
}

void test_individual_signals(TestResults& results) {
    std::cout << "\n--- Test: Individual Signals ---\n";
    
    AIDetector detector;
    
    // High vocabulary match, normal structure
    std::string vocab_heavy = R"(
It's important to note that we need to delve into this topic. The comprehensive 
nature of the subject requires us to leverage multiple approaches. Furthermore, 
the multifaceted landscape presents myriad challenges.

At the end of the day, fostering understanding is crucial. We must utilize 
all available resources to enhance our knowledge. Let's explore the pivotal 
aspects of this fascinating realm.
    )";
    
    float vocab = detector.vocabulary_score(vocab_heavy);
    std::cout << "  Vocab-heavy text vocab_score: " << std::fixed << std::setprecision(3) << vocab << "\n";
    print_test(vocab > 0.3f, "High vocab score for AI phrases", results);
    
    // Uniform sentence length
    std::string uniform_text = R"(
The cat sat on the mat. The dog ran in the yard. The bird flew in the sky.
The fish swam in the pond. The horse ran on the track. The cow stood in the field.
The pig rolled in the mud. The chicken pecked at the ground. The duck swam in the lake.
    )";
    
    float uniform = detector.uniformity_score(uniform_text);
    std::cout << "  Uniform sentences uniformity_score: " << std::fixed << std::setprecision(3) << uniform << "\n";
    print_test(uniform > 0.5f, "High uniformity for uniform sentences", results);
    
    // Varied sentence length (human-like)
    std::string varied_text = R"(
I went to the store. It was a really long walk through the neighborhood, past 
the old church and the elementary school where I used to play basketball. 
Got milk. The cashier was new—young kid, probably his first job. Walked home. 
Tomorrow I should probably drive instead of walking because the weather forecast 
says it might rain, and I don't want to get caught in a downpour again like 
last Tuesday when I forgot my umbrella and had to run the last three blocks. 
Simple trip.
    )";
    
    float varied = detector.uniformity_score(varied_text);
    std::cout << "  Varied sentences uniformity_score: " << std::fixed << std::setprecision(3) << varied << "\n";
    print_test(varied < 0.5f, "Low uniformity for varied sentences", results);
}

void test_edge_cases(TestResults& results) {
    std::cout << "\n--- Test: Edge Cases ---\n";
    
    AIDetector detector;
    
    // Too short
    float short_score = detector.calculate_score("Hello world.");
    print_test(short_score == 0.0f, "Short text returns 0", results);
    
    // Empty
    float empty_score = detector.calculate_score("");
    print_test(empty_score == 0.0f, "Empty text returns 0", results);
    
    // Single long paragraph
    std::string single_para = R"(
This is a single long paragraph without any breaks that just keeps going and 
going with various topics mentioned including technology and science and art 
and music and literature and history and philosophy and mathematics and physics 
and chemistry and biology all mixed together in one continuous stream of text 
that never really stops or pauses for breath it just keeps flowing like a river 
through the countryside picking up speed as it goes downhill toward the ocean.
    )";
    
    float single_score = detector.calculate_score(single_para);
    std::cout << "  Single paragraph score: " << std::fixed << std::setprecision(3) << single_score << "\n";
    print_test(single_score >= 0.0f && single_score <= 1.0f, "Single paragraph handled", results);
}

void test_separation(TestResults& results) {
    std::cout << "\n--- Test: Score Separation ---\n";
    
    AIDetector detector;
    
    // Get scores for both types
    std::string human = R"(
I woke up this morning thinking about coffee. Not in a philosophical way, 
just—I really wanted some. The machine was broken again. So I walked to the 
cafe down the street. It's one of those places where the barista remembers 
your order. Small town stuff. The coffee was good. Really good, actually. 
Sat there for an hour reading the paper. An actual paper newspaper! They 
still print those, apparently. Back home now. Machine's still broken. Maybe 
I'll fix it tomorrow. Or maybe I'll just keep walking to the cafe.
    )";
    
    std::string ai = R"(
In today's rapidly evolving digital landscape, it's important to note that 
artificial intelligence has become a crucial component. Let's delve into the 
multifaceted world of AI. Furthermore, the comprehensive nature of AI systems 
allows for seamless integration. Additionally, these robust solutions leverage 
cutting-edge algorithms. Moreover, it's essential to understand that AI plays 
a pivotal role. In conclusion, the tapestry of artificial intelligence 
continues to expand. At the end of the day, AI represents a paradigm shift.
    )";
    
    float human_score = detector.calculate_score(human);
    float ai_score = detector.calculate_score(ai);
    
    std::cout << "  Human: " << std::fixed << std::setprecision(3) << human_score;
    std::cout << "  AI: " << ai_score << "\n";
    
    float separation = ai_score - human_score;
    std::cout << "  Separation: " << separation << "\n";
    
    print_test(separation > 0.2f, "Good separation (>0.2) between human and AI", results);
}

int main() {
    std::cout << "\n============================================================\n";
    std::cout << "    AI Detector Heuristics Tests                             \n";
    std::cout << "============================================================\n";
    
    TestResults results;
    
    test_human_text(results);
    test_ai_text(results);
    test_individual_signals(results);
    test_edge_cases(results);
    test_separation(results);
    
    std::cout << "\n============================================================\n";
    std::cout << "Passed: " << results.passed << " | Failed: " << results.failed << "\n";
    std::cout << (results.failed == 0 ? "[SUCCESS]" : "[NEEDS TUNING]") << "\n\n";
    
    return results.failed;
}