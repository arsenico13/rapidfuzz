#include "fuzz.hpp"
#include "levenshtein.hpp"
#include "utils.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
#include <limits>
#include <iterator>


percent fuzz::partial_ratio(std::wstring s1, std::wstring s2, percent score_cutoff, bool preprocess) {
  if (score_cutoff >= 100) {
    return 0;
  }

  if (preprocess) {
    s1 = utils::default_process(std::move(s1));
    s2 = utils::default_process(std::move(s2));
  }

  if (s1.empty() || s2.empty()) {
    return 0;
  }

  std::wstring_view shorter;
  std::wstring_view longer;

  if (s1.length() > s2.length()) {
    shorter = s2;
    longer = s1;
  } else {
    shorter = s1;
    longer = s2;
  }

  auto blocks = levenshtein::matching_blocks(shorter, longer);
  float max_ratio = 0;
  for (const auto &block : blocks) {
      std::size_t long_start = (block.second_start > block.first_start) ? block.second_start - block.first_start : 0;
      std::wstring_view long_substr = longer.substr(long_start, shorter.length());

      float ls_ratio = levenshtein::normalized_weighted_distance(shorter, long_substr, score_cutoff / 100);

      if (ls_ratio > 0.995) {
  			return 100;
  		}

      if (ls_ratio > max_ratio) {
  			max_ratio = ls_ratio;
  		}
  }

  return utils::result_cutoff(max_ratio*100, score_cutoff);
}


percent fuzz::ratio(const std::wstring &s1, const std::wstring &s2, percent score_cutoff, bool preprocess) {
  float result;
  if (preprocess) {
    result = levenshtein::normalized_weighted_distance(
      utils::default_process(s1), utils::default_process(s2), score_cutoff / 100);
  } else {
    result = levenshtein::normalized_weighted_distance(s1, s2, score_cutoff / 100);
  }

  return utils::result_cutoff(result*100, score_cutoff);
}


percent fuzz::token_ratio(const std::wstring &s1, const std::wstring &s2, percent score_cutoff, bool preprocess) {
  if (score_cutoff >= 100) {
    return 0;
  }

  std::wstring a;
  std::wstring b;
  if (preprocess) {
    a = utils::default_process(s1);
    b = utils::default_process(s2);
  } else {
    a = s1;
    b = s2;
  }
  std::vector<std::wstring_view> tokens_a = utils::splitSV(a);
  std::sort(tokens_a.begin(), tokens_a.end());
  std::vector<std::wstring_view> tokens_b = utils::splitSV(b);
  std::sort(tokens_b.begin(), tokens_b.end());

  auto [intersection, difference_ab, difference_ba] = utils::set_decomposition(tokens_a, tokens_b);

  std::size_t ab_len = utils::joined_size(difference_ab);
  std::size_t ba_len = utils::joined_size(difference_ba);
  std::size_t double_prefix = 2 * utils::joined_size(intersection);

  // fuzzywuzzy joined sect and ab/ba for comparisions
  // this is not done here as an optimisation, so the lengths get incremented by 1
  // since there would be a whitespace between the joined strings
  if (double_prefix) {
    // exit early since this will always result in a ratio of 1
    if (!ab_len || !ba_len) return 100;

    ++ab_len;
    ++ba_len;
  }

  float result = levenshtein::normalized_weighted_distance(tokens_a, tokens_b, score_cutoff / 100);

  // TODO: could add score cutoff aswell, but would need to copy most things from normalized_score_cutoff
  // as an alternative add another utility function to levenshtein for this case
  std::size_t sect_distance = levenshtein::weighted_distance(difference_ab, difference_ba);
  if (sect_distance != std::numeric_limits<std::size_t>::max()) {
    std::size_t lensum = ab_len + ba_len + double_prefix;
    result = std::max(result, (float)1.0 - sect_distance / (float)lensum);
  }

  // exit early since the other ratios are 0
  // (when a or b was empty they would cause a segfault)
  if (!double_prefix) {
    return utils::result_cutoff(result*100, score_cutoff);
  }

  result = std::max({
    result,
    // levenshtein distances sect+ab <-> sect and sect+ba <-> sect
    // would exit early after removing the prefix sect, so the distance can be directly calculated
    (float)1.0 - (float)ab_len / (float)(ab_len + double_prefix),
    (float)1.0 - (float)ba_len / (float)(ba_len + double_prefix)
  });
  return utils::result_cutoff(result*100, score_cutoff);
}


// combines token_set and token_sort ratio from fuzzywuzzy so it is only required to
// do a lot of operations once
percent fuzz::partial_token_ratio(const std::wstring &s1, const std::wstring &s2, percent score_cutoff, bool preprocess) {
  if (score_cutoff >= 100) {
    return 0;
  }

  std::wstring a;
  std::wstring b;
  if (preprocess) {
    a = utils::default_process(s1);
    b = utils::default_process(s2);
  } else {
    a = s1;
    b = s2;
  }

  std::vector<std::wstring_view> tokens_a = utils::splitSV(a);
  std::sort(tokens_a.begin(), tokens_a.end());
  std::vector<std::wstring_view> tokens_b = utils::splitSV(b);
  std::sort(tokens_b.begin(), tokens_b.end());

  auto unique_a = tokens_a;
  auto unique_b = tokens_b;
  unique_a.erase(std::unique(unique_a.begin(), unique_a.end()), unique_a.end());
  unique_b.erase(std::unique(unique_b.begin(), unique_b.end()), unique_b.end());
    
  std::vector<std::wstring_view> difference_ab;
  std::vector<std::wstring_view> difference_ba;

  std::set_difference(unique_a.begin(), unique_a.end(), unique_b.begin(), unique_b.end(), 
                      std::inserter(difference_ab, difference_ab.begin()));
  std::set_difference(unique_b.begin(), unique_b.end(), unique_a.begin(), unique_a.end(), 
                      std::inserter(difference_ba, difference_ba.begin()));

  // exit early when there is a common word in both sequences
  if (difference_ab.size() < unique_a.size()) {
    return 100;
  }

  percent result = partial_ratio(utils::join(tokens_a), utils::join(tokens_b), score_cutoff, false);
  // do not calculate the same partial_ratio twice
  if (tokens_a.size() == unique_a.size() && tokens_b.size() == unique_b.size()) {
    return result;
  }

  score_cutoff = std::max(score_cutoff, result);
  return std::max(
    result,
    partial_ratio(utils::join(difference_ab), utils::join(difference_ba), score_cutoff, false)
  );
}


percent _token_sort(const std::wstring &s1, const std::wstring &s2, bool partial, percent score_cutoff=0.0, bool preprocess = true) {
  if (score_cutoff >= 100) {
    return 0;
  }

  std::wstring a;
  std::wstring b;
  if (preprocess) {
    a = utils::default_process(s1);
    b = utils::default_process(s2);
  } else {
    a = s1;
    b = s2;
  }

  std::vector<std::wstring_view> tokens_a = utils::splitSV(a);
  std::sort(tokens_a.begin(), tokens_a.end());
  std::vector<std::wstring_view> tokens_b = utils::splitSV(b);
  std::sort(tokens_b.begin(), tokens_b.end());

  if (partial) {
    return fuzz::partial_ratio(utils::join(tokens_a), utils::join(tokens_b), score_cutoff, false);
  } else {
    float result = levenshtein::normalized_weighted_distance(tokens_a, tokens_b, score_cutoff / 100);
    return utils::result_cutoff(result*100, score_cutoff);
  } 
}


percent fuzz::token_sort_ratio(const std::wstring &a, const std::wstring &b, percent score_cutoff, bool preprocess) {
  return _token_sort(a, b, false, score_cutoff);
}


percent fuzz::partial_token_sort_ratio(const std::wstring &a, const std::wstring &b, percent score_cutoff, bool preprocess) {
  return _token_sort(a, b, true, score_cutoff);
}


percent fuzz::token_set_ratio(const std::wstring &s1, const std::wstring &s2, percent score_cutoff, bool preprocess) {
  if (score_cutoff >= 100) {
    return 0;
  }

  std::wstring a;
  std::wstring b;
  if (preprocess) {
    a = utils::default_process(s1);
    b = utils::default_process(s2);
  } else {
    a = s1;
    b = s2;
  }

  std::vector<std::wstring_view> tokens_a = utils::splitSV(a);
  std::sort(tokens_a.begin(), tokens_a.end());
  std::vector<std::wstring_view> tokens_b = utils::splitSV(b);
  std::sort(tokens_b.begin(), tokens_b.end());

  auto [intersection, difference_ab, difference_ba] = utils::set_decomposition(tokens_a, tokens_b);

  std::size_t ab_len = utils::joined_size(difference_ab);
  std::size_t ba_len = utils::joined_size(difference_ba);
  std::size_t double_prefix = 2 * utils::joined_size(intersection);

  // fuzzywuzzy joined sect and ab/ba for comparisions
  // this is not done here as an optimisation, so the lengths get incremented by 1
  // since there would be a whitespace between the joined strings
  if (double_prefix) {
    // exit early since this will always result in a ratio of 1
    if (!ab_len || !ba_len) return 100;

    ++ab_len;
    ++ba_len;
  }

  // TODO: could add score cutoff aswell, but would need to copy most things from normalized_score_cutoff
  // as an alternative add another utility function to levenshtein for this case
  std::size_t sect_distance = levenshtein::weighted_distance(difference_ab, difference_ba);
  float result = 0;
  if (sect_distance != std::numeric_limits<std::size_t>::max()) {
    std::size_t lensum = ab_len + ba_len + double_prefix;
    result = (float)1.0 - sect_distance / (float)lensum;
  }

  // exit early since the other ratios are 0
  // (when a or b was empty they would cause a segfault)
  if (!double_prefix) {
    return utils::result_cutoff(result*100, score_cutoff);
  }

  result = std::max({
    result,
    // levenshtein distances sect+ab <-> sect and sect+ba <-> sect
    // would exit early after removing the prefix sect, so the distance can be directly calculated
    (float)1.0 - (float)ab_len / (float)(ab_len + double_prefix),
    (float)1.0 - (float)ba_len / (float)(ba_len + double_prefix)
  });

  return utils::result_cutoff(result*100, score_cutoff);
}


percent fuzz::partial_token_set_ratio(const std::wstring &s1, const std::wstring &s2, percent score_cutoff, bool preprocess) {
  if (score_cutoff >= 100) {
    return 0;
  }

  std::wstring a;
  std::wstring b;
  if (preprocess) {
    a = utils::default_process(s1);
    b = utils::default_process(s2);
  } else {
    a = s1;
    b = s2;
  }

  std::vector<std::wstring_view> tokens_a = utils::splitSV(a);
  std::sort(tokens_a.begin(), tokens_a.end());
  std::vector<std::wstring_view> tokens_b = utils::splitSV(b);
  std::sort(tokens_b.begin(), tokens_b.end());

  tokens_a.erase(std::unique(tokens_a.begin(), tokens_a.end()), tokens_a.end());
  tokens_b.erase(std::unique(tokens_b.begin(), tokens_b.end()), tokens_b.end());
    
  std::vector<std::wstring_view> difference_ab;
  std::vector<std::wstring_view> difference_ba;

  std::set_difference(tokens_a.begin(), tokens_a.end(), tokens_b.begin(), tokens_b.end(), 
                      std::inserter(difference_ab, difference_ab.begin()));
  std::set_difference(tokens_b.begin(), tokens_b.end(), tokens_a.begin(), tokens_a.end(), 
                      std::inserter(difference_ba, difference_ba.begin()));

  // exit early when there is a common word in both sequences
  if (difference_ab.size() < tokens_a.size()) {
    return 100;
  }

  return partial_ratio(utils::join(difference_ab), utils::join(difference_ba), score_cutoff, false);
}


percent fuzz::WRatio(const std::wstring &s1, const std::wstring &s2, percent score_cutoff, bool preprocess) {
  if (score_cutoff >= 100) {
    return 0;
  }

  std::wstring a;
  std::wstring b;
  if (preprocess) {
    a = utils::default_process(s1);
    b = utils::default_process(s2);
  } else {
    a = s1;
    b = s2;
  }

  const float UNBASE_SCALE = 0.95;

  std::size_t len_a = a.length();
  std::size_t len_b = b.length();
  float len_ratio = (len_a > len_b) ? (float)len_a / (float)len_b : (float)len_b / (float)len_a;

  float sratio = ratio(a, b, score_cutoff, false);

  if (len_ratio < 1.5) {
    score_cutoff = std::max(score_cutoff, sratio);
    return std::max(sratio, token_ratio(a, b, score_cutoff/UNBASE_SCALE, false) * UNBASE_SCALE);
  }

  float partial_scale = (len_ratio < 8.0) ? 0.9 : 0.6;

  score_cutoff = std::max(score_cutoff, sratio)/partial_scale;
  sratio = std::max(sratio, partial_ratio(a, b, score_cutoff, false) * partial_scale);

  score_cutoff = std::max(score_cutoff, sratio)/UNBASE_SCALE;
  return std::max(sratio, partial_token_ratio(a, b, score_cutoff, false) * UNBASE_SCALE * partial_scale );
}
