
#include "board.hpp"
#include "piece.hpp"
#include "hash.hpp"
#include "move.hpp"

#include <algorithm>
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include <map>

Board::Board(const std::string &fen) noexcept {
  m_pieces.fill(INVALID_PIECE);
  m_num_pieces.fill(0);
  for (unsigned piece = 0; piece < 16; ++piece) {
    m_positions[piece].fill(INVALID_SQUARE);
  }

  // Part 1: The board state
  unsigned square_idx = A8; // 91
  const char* next_chr = fen.data();
  const char* const end_ptr = fen.data() + fen.size();
  while (*next_chr != ' ') {
    const char chr = *next_chr;
    if (chr == '/') {
      // To the next row
      ASSERT(square_idx % 10 == 9);
      square_idx -= 18;
    } else if ('1' <= chr && chr <= '8') {
      // A number indicates that number of empty squares
      const unsigned num_spaces = chr - '0';
      for (unsigned i = 0; i < num_spaces; ++i)
        m_pieces[square_idx + i] = INVALID_PIECE;
      square_idx += num_spaces;
      ASSERT(square_idx % 10 == 9 || valid_square(square_idx));
    } else {
      // Otherwise, it better be a character corresponding to a valid piece
      const piece_t piece_idx = piece_from_char(chr);
      ASSERT(valid_square(square_idx));
      ASSERT(valid_piece(piece_idx));
      m_pieces[square_idx] = piece_idx;
      ASSERT_MSG(m_num_pieces[piece_idx] < MAX_PIECE_FREQ,
        "Too many (%u) pieces of type %u", m_num_pieces[piece_idx], piece_idx);
      m_positions[piece_idx][m_num_pieces[piece_idx]] = square_idx;
      m_num_pieces[piece_idx]++;
      square_idx++;
      ASSERT(square_idx % 10 == 9 || valid_square(square_idx));
    }
    next_chr++;
  }
  ASSERT_MSG(square_idx == 29,
    "Unexpected last square (%u) after parsing FEN board", square_idx);

  // Part 2: Side to move
  next_chr++;
  ASSERT_MSG(*next_chr == 'w' || *next_chr == 'b',
    "Invalid FEN side (%c)", *next_chr);
  m_next_move_colour = (*next_chr == 'w') ? WHITE : BLACK;

  // Part 3: Castle state
  next_chr += 2;
  m_castle_state = 0;
  if (*next_chr == '-') {
    next_chr++;
  } else {
    while (*next_chr != ' ') {
      switch (*next_chr) {
        case 'K':
          m_castle_state |= WHITE_SHORT; break;
        case 'Q':
          m_castle_state |= WHITE_LONG; break;
        case 'k':
          m_castle_state |= BLACK_SHORT; break;
        case 'q':
          m_castle_state |= BLACK_LONG; break;
        default:
          ASSERT_MSG(0,
            "Invalid character (%c) in castling permission specifications",
            *next_chr);
      }
      next_chr++;
    }
  }
  ASSERT(*next_chr == ' ');

  // Part 4: En passant square
  // TODO: Check whether an en passant capture is even possible.
  // If not, zero the en passant square as it is irrelevant.
  next_chr++;
  if (*next_chr == '-') {
    m_en_passant = INVALID_SQUARE;
    next_chr += 1;
  } else {
    const unsigned row = *(next_chr + 1) - '1', col = *next_chr - 'a';
    ASSERT_IF(m_next_move_colour == WHITE, row == RANK_6);
    ASSERT_IF(m_next_move_colour == BLACK, row == RANK_3);
    m_en_passant = get_square_120_rc(row, col);
    const piece_t my_pawn = (m_next_move_colour == WHITE) ? WHITE_PAWN : BLACK_PAWN;
    if (m_pieces[m_en_passant - 1] != my_pawn
      && m_pieces[m_en_passant + 1] != my_pawn) {
      WARN("Elided en passant square");
      m_en_passant = INVALID_SQUARE;
    }
    next_chr += 2;
  }
  ASSERT(*next_chr == ' ');

  // Part 5: Half move counter
  m_fifty_move = 0;
  next_chr++;
  while (*next_chr != ' ') {
    ASSERT_MSG('0' <= *next_chr && *next_chr <= '9',
      "Invalid digit (%c) in half move counter", *next_chr);
    m_fifty_move = 10 * m_fifty_move + (*next_chr - '0');
    next_chr++;
  }
  ASSERT(*next_chr == ' ');

  // Part 6: Full move counter
  size_t full_move = 0;
  next_chr++;
  while (next_chr != end_ptr) {
    ASSERT_MSG('0' <= *next_chr && *next_chr <= '9',
      "Invalid digit (%c) in full move counter", *next_chr);
    full_move = 10 * full_move + (*next_chr - '0');
    next_chr++;
  }
  m_half_move = 2 * full_move + m_next_move_colour;
  ASSERT_MSG(next_chr == end_ptr, "FEN string too long");

  m_hash = compute_hash();
  validate_board();
}

void Board::validate_board() const noexcept {
#if defined(DEBUG)
  static std::array<unsigned, 16> piece_count;
  piece_count.fill(0);
  for (unsigned sq = 0; sq < 120; ++sq) {
    ASSERT_MSG(valid_piece(m_pieces[sq]) || m_pieces[sq] == INVALID_PIECE,
      "Piece %u at %u is neither valid nor INVALID_PIECE", m_pieces[sq], sq);
    piece_count[m_pieces[sq]]++;
  }
  ASSERT_MSG(m_num_pieces[WHITE_KING] == 1,
    "White has too few/many (%u) kings", m_num_pieces[WHITE_KING]);
  ASSERT_MSG(m_num_pieces[BLACK_KING] == 1,
    "Black has too few/many (%u) kings", m_num_pieces[BLACK_KING]);
  for (unsigned piece = 0; piece < 16; ++piece) {
    ASSERT_IF_MSG(!valid_piece(piece), m_num_pieces[piece] == 0,
      "Invalid piece %u has non-zero count %u", piece, m_num_pieces[piece]);
    const unsigned end = m_num_pieces[piece];
    ASSERT_IF_MSG(valid_piece(piece), m_num_pieces[piece] == piece_count[piece],
      "Too few/many (%u) pieces of type %u, expected %u",
        m_num_pieces[piece], piece, piece_count[piece]);
    ASSERT_MSG(end <= MAX_PIECE_FREQ,
      "Too many (%u) pieces of type %u", end, piece);
    for (unsigned num = 0; num < end; ++num) {
      const square_t sq = m_positions[piece][num];
      ASSERT_MSG(m_pieces[sq] == piece,
        "m_positions[%u][%u] inconsistent with m_pieces[%u]", piece, num, sq);
      for (unsigned compare_idx = num + 1; compare_idx < end; ++compare_idx) {
        ASSERT_MSG(m_positions[piece][num] != m_positions[piece][compare_idx],
          "Repeated position of piece %u at indices %u and %u",
            piece, num, compare_idx);
      }
    }
  }
  ASSERT_MSG(0 <= m_castle_state && m_castle_state < 16,
    "Castle state (%u) out of range", m_castle_state);
  ASSERT_MSG(valid_square(m_en_passant) || m_en_passant == INVALID_SQUARE,
    "En passant square (%u) not valid nor INVALID_SQUARE", m_en_passant);
  ASSERT_IF_MSG(m_next_move_colour == BLACK && m_en_passant != INVALID_SQUARE,
    get_square_row(m_en_passant) == RANK_3,
    "En passant square (%s - %u) not on row 3 on black's turn",
      string_from_square(m_en_passant).c_str(), m_en_passant);
  ASSERT_IF_MSG(m_next_move_colour == WHITE && m_en_passant != INVALID_SQUARE,
    get_square_row(m_en_passant) == RANK_6,
    "En passant square (%s - %u) not on row 6 on white's turn",
      string_from_square(m_en_passant).c_str(), m_en_passant);

  // Assert other king is not in check
  const piece_t king_piece = (m_next_move_colour == BLACK) ? WHITE_KING : BLACK_KING;
  const square_t king_square = m_positions[king_piece][0];
  ASSERT_MSG(!square_attacked(king_square, m_next_move_colour),
    "Other king on (%s) did not avoid check", string_from_square(king_square).c_str());
#endif
}

std::string Board::fen() const noexcept {
  std::stringstream result;
  validate_board();

  // Part 1. The board state
  unsigned square_idx = A8;
  unsigned blank_count = 0;
  while (square_idx != 29) {
    if (square_idx % 10 == 9) {
      if (blank_count != 0) {
        ASSERT_MSG(blank_count <= 8,
          "Too many (%u) blank squares in a row", blank_count);
        result << (char)('0' + blank_count);
        blank_count = 0;
      }
      result << '/';
      square_idx -= 18;
    }
    const piece_t piece = m_pieces[square_idx];
    if (piece == INVALID_PIECE) {
      blank_count++;
    } else {
      // Piece will be valid as board was validated
      if (blank_count != 0) {
        result << (char)('0' + blank_count);
        blank_count = 0;
      }
      result << char_from_piece(piece);
    }
    square_idx++;
  }
  if (blank_count != 0) {
    result << (char)('0' + blank_count);
  }
  result << ' ';

  // Part 2: Side to move
  result << (m_next_move_colour == WHITE ? 'w' : 'b') << ' ';

  // Part 3: Castle state
  if (m_castle_state == 0) {
    result << '-';
  } else {
    if (m_castle_state & WHITE_SHORT)
      result << 'K';
    if (m_castle_state & WHITE_LONG)
      result << 'Q';
    if (m_castle_state & BLACK_SHORT)
      result << 'k';
    if (m_castle_state & BLACK_LONG)
      result << 'q';
  }
  result << ' ';

  // Part 4: En passant square
  result << string_from_square(m_en_passant) << ' ';

  // Part 5: Half move counter
  result << m_fifty_move << ' ';

  // Part 6: Full move counter
  result << (m_half_move / 2);

  return result.str();
}

hash_t Board::compute_hash() const noexcept {
  validate_board();
  hash_t res = 0;
  for (unsigned sq = 0; sq < 120; ++sq) {
    const piece_t piece = m_pieces[sq];
    ASSERT_MSG(0 <= piece && piece < 16,
      "Out of range piece (%u) in square", piece);
    ASSERT_MSG(valid_piece(piece) || piece_hash[sq][piece] == 0,
      "Invalid piece (%u) had non-zero hash (%llu)",
        piece, piece_hash[sq][piece]);
    res ^= piece_hash[sq][piece];
  }
  res ^= castle_hash[m_castle_state];
  ASSERT_MSG(enpas_hash[INVALID_SQUARE] == 0,
    "Invalid square had non-zero hash (%llu)", enpas_hash[INVALID_SQUARE]);
  res ^= enpas_hash[m_en_passant];
  res ^= (m_next_move_colour * side_hash);
  return res;
}

std::string Board::to_string() const noexcept {
  validate_board();
  std::stringstream result;
  result << "+---- BOARD ----+" << '\n';
  for (int row = 7; row >= 0; --row) {
    result << '|';
    for (int col = 0; col < 8; ++col) {
      const square_t square = get_square_120_rc(row, col);
      const piece_t piece_idx = m_pieces[square];
      result << char_from_piece(piece_idx) << '|';
    }
    result << '\n';
  }
  result << "+---------------+\n";
  result << "TO MOVE: ";
  result << ((m_next_move_colour == WHITE) ? "WHITE" : "BLACK") << '\n';
  result << "EN PASS: " << string_from_square(m_en_passant) << '\n';
  result << "FIFTY  : " << m_fifty_move << '\n';
  result << "MOVE#  : " << (m_half_move / 2) << '\n';
  result << "HALF#  : " << m_half_move << '\n';
  result << "HASH   : ";
  result << std::setw(16) << std::setfill('0') << std::hex << hash() << '\n';
  result << "FEN    : " << fen() << '\n';
  if (!m_history.empty()) {
    result << "LAST MV: " << string_from_move(m_history.back().move) << '\n';
  }
  return result.str();
}

std::ostream& operator<<(std::ostream &os, const Board& board) noexcept {
  return os << board.to_string();
}

bool Board::square_attacked(const square_t sq, const bool side) const noexcept {
  const piece_t king_piece   = (side == WHITE) ? WHITE_KING : BLACK_KING,
                knight_piece = (side == WHITE) ? WHITE_KNIGHT : BLACK_KNIGHT,
                pawn_piece   = (side == WHITE) ? WHITE_PAWN   : BLACK_PAWN;
  const square_t king_square = m_positions[king_piece][0];

  if (valid_piece(m_pieces[sq]) && get_side(m_pieces[sq] == side)) {
    ASSERT_MSG(get_side(m_pieces[sq]) != side, "Querying square attacked of own piece");
    return false;
  }

  // Diagonals
  const auto &diagonal_offsets = {-11, -9, 9, 11};
  for (const int offset : diagonal_offsets) {
    square_t cur_square = sq + offset;
    if (king_square == cur_square)
      return true;
    while (valid_square(cur_square) && m_pieces[cur_square] == INVALID_PIECE)
      cur_square += offset;
    const piece_t cur_piece = m_pieces[cur_square];
    if (valid_square(cur_square) && get_side(cur_piece) == side && is_diag(cur_piece))
      return true;
  }

  // Orthogonals
  const auto &orthogonal_offsets = {-10, -1, 1, 10};
  for (const int offset : orthogonal_offsets) {
    square_t cur_square = sq + offset;
    if (king_square == cur_square)
      return true;
    while (valid_square(cur_square) && m_pieces[cur_square] == INVALID_PIECE)
      cur_square += offset;
    const piece_t cur_piece = m_pieces[cur_square];
    if (valid_square(cur_square) && get_side(cur_piece) == side && is_ortho(cur_piece))
      return true;
  }

  // Knights
  const auto &knight_offsets = {-21, -19, -12, -8, 8, 12, 19, 21};
  for (const int offset : knight_offsets)
    if (m_pieces[sq + offset] == knight_piece)
      return true;

  // Pawns
  const auto &pawn_offsets = {(side == WHITE) ? -9 : 9, (side == WHITE) ? -11 : 11};
  for (const int offset : pawn_offsets)
    if (m_pieces[sq + offset] == pawn_piece)
      return true;

  return false;
}

bool Board::king_in_check() const noexcept {
  const piece_t king_piece = (m_next_move_colour == WHITE) ? WHITE_KING : BLACK_KING;
  return square_attacked(m_positions[king_piece][0], !m_next_move_colour);
}

std::vector<move_t> Board::pseudo_moves(const int _side) const noexcept {
  const auto &it = m_move_cache.find(m_hash);
  if (it != m_move_cache.end())
    return it->second;

  validate_board();

  std::vector<move_t> result;
  if (m_half_move > 1000 || m_fifty_move > 75)
    return result; // 50 (75) move rule
  result.reserve(MAX_POSITION_MOVES);

  const int side = (_side != INVALID_SIDE) ? _side : m_next_move_colour;
  ASSERT_MSG(side == WHITE || side == BLACK, "Invalid side (%u)", side);

  const piece_t king_piece   = (side == WHITE) ? WHITE_KING   : BLACK_KING,
                queen_piece  = (side == WHITE) ? WHITE_QUEEN  : BLACK_QUEEN,
                rook_piece   = (side == WHITE) ? WHITE_ROOK   : BLACK_ROOK,
                bishop_piece = (side == WHITE) ? WHITE_BISHOP : BLACK_BISHOP,
                knight_piece = (side == WHITE) ? WHITE_KNIGHT : BLACK_KNIGHT,
                pawn_piece   = (side == WHITE) ? WHITE_PAWN   : BLACK_PAWN;

  const piece_t promote_pieces[4] = {
    static_cast<piece_t>(queen_piece ^ 8u),  static_cast<piece_t>(rook_piece ^ 8u),
    static_cast<piece_t>(bishop_piece ^ 8u), static_cast<piece_t>(knight_piece ^ 8u),
  };

  // Queens
  for (unsigned queen_idx = 0; queen_idx < m_num_pieces[queen_piece]; ++queen_idx) {
    const square_t start = m_positions[queen_piece][queen_idx];
    for (int offset : {-11, -10, -9, -1, 1, 9, 10, 11}) {
      square_t cur_square = start + offset;
      while (valid_square(cur_square) && m_pieces[cur_square] == INVALID_PIECE) {
        // printf("Quiet move from %s to %s\n",
        //   string_from_square(start).c_str(), string_from_square(cur_square).c_str());
        result.push_back(quiet_move(start, cur_square, queen_piece));
        cur_square += offset;
      }
      if (valid_square(cur_square) && opposite_colours(queen_piece, m_pieces[cur_square]) && !is_king(m_pieces[cur_square])) {
        // printf("Capture move from %s to %s\n",
        //  string_from_square(start).c_str(), string_from_square(cur_square).c_str());
        result.push_back(capture_move(start, cur_square, queen_piece, m_pieces[cur_square]));
      }
    }
  }

  // Rooks
  for (unsigned rook_idx = 0; rook_idx < m_num_pieces[rook_piece]; ++rook_idx) {
    const square_t start = m_positions[rook_piece][rook_idx];
    for (int offset : {-10, -1, 1, 10}) {
      square_t cur_square = start + offset;
      while (valid_square(cur_square) && m_pieces[cur_square] == INVALID_PIECE) {
        // printf("Quiet move from %s to %s\n",
        //   string_from_square(start).c_str(), string_from_square(cur_square).c_str());
        result.push_back(quiet_move(start, cur_square, rook_piece));
        cur_square += offset;
      }
      if (valid_square(cur_square) && opposite_colours(rook_piece, m_pieces[cur_square]) && !is_king(m_pieces[cur_square])) {
        // printf("Capture move from %s to %s\n",
        //  string_from_square(start).c_str(), string_from_square(cur_square).c_str());
        result.push_back(capture_move(start, cur_square, rook_piece, m_pieces[cur_square]));
      }
    }
  }

  // Bishops
  for (unsigned bishop_idx = 0; bishop_idx < m_num_pieces[bishop_piece]; ++bishop_idx) {
    const square_t start = m_positions[bishop_piece][bishop_idx];
    for (const int offset : {-11, -9, 9, 11}) {
      square_t cur_square = start + offset;
      while (valid_square(cur_square) && m_pieces[cur_square] == INVALID_PIECE) {
        // printf("Quiet move from %s to %s\n",
        //   string_from_square(start).c_str(), string_from_square(cur_square).c_str());
        result.push_back(quiet_move(start, cur_square, bishop_piece));
        cur_square += offset;
      }
      if (valid_square(cur_square) && opposite_colours(bishop_piece, m_pieces[cur_square]) && !is_king(m_pieces[cur_square])) {
        // printf("Capture move from %s to %s\n",
        //  string_from_square(start).c_str(), string_from_square(cur_square).c_str());
        result.push_back(capture_move(start, cur_square, bishop_piece, m_pieces[cur_square]));
      }
    }
  }

  // Knights
  for (unsigned knight_idx = 0; knight_idx < m_num_pieces[knight_piece]; ++knight_idx) {
    const square_t start = m_positions[knight_piece][knight_idx];
    for (const int offset : {-21, -19, -12, -8, 8, 12, 19, 21}) {
      const square_t cur_square = start + offset;
      if (valid_square(cur_square) && m_pieces[cur_square] == INVALID_PIECE)
        result.push_back(quiet_move(start, cur_square, knight_piece));
      else if (valid_square(cur_square) && opposite_colours(knight_piece, m_pieces[cur_square]) && !is_king(m_pieces[cur_square]))
        result.push_back(capture_move(start, cur_square, knight_piece, m_pieces[cur_square]));
    }
  }

  // Pawns
  for (unsigned pawn_idx = 0; pawn_idx < m_num_pieces[pawn_piece]; ++pawn_idx) {
    const square_t start = m_positions[pawn_piece][pawn_idx];
    // Double pawn moves
    if (side == WHITE && get_square_row(start) == RANK_2
      && m_pieces[start + 10] == INVALID_PIECE && m_pieces[start + 20] == INVALID_PIECE) {
      result.push_back(double_move(start, start + 20, pawn_piece));
    }
    if (side == BLACK && get_square_row(start) == RANK_7
      && m_pieces[start - 10] == INVALID_PIECE && m_pieces[start - 20] == INVALID_PIECE) {
      result.push_back(double_move(start, start - 20, pawn_piece));
    }

    // Single pawn moves
    const int offset = (side == WHITE) ? 10 : -10;
    const square_t cur_square = start + offset;
    if (valid_square(cur_square) && m_pieces[cur_square] == INVALID_PIECE) {
      if (get_square_row(cur_square) == RANK_1 || get_square_row(cur_square) == RANK_8) {
        for (const piece_t promote_piece : promote_pieces) {
          result.push_back(promote_move(start, cur_square, pawn_piece, promote_piece));
        }
      } else {
        result.push_back(quiet_move(start, start + offset, pawn_piece));
      }
    }

    // Normal capture moves
    const square_t capture1 = cur_square - 1, capture2 = cur_square + 1;
    if (valid_square(capture1)
      && m_pieces[capture1] != INVALID_PIECE
      && opposite_colours(pawn_piece, m_pieces[capture1]) && !is_king(m_pieces[capture1])) {
      if (get_square_row(capture1) == RANK_1 || get_square_row(capture1) == RANK_8) {
        for (const piece_t promote_piece : promote_pieces) {
          result.push_back(promote_capture_move(start, capture1, pawn_piece, promote_piece, m_pieces[capture1]));
        }
      } else {
        result.push_back(capture_move(start, capture1, pawn_piece, m_pieces[capture1]));
      }
    }
    if (valid_square(capture2)
      && m_pieces[capture2] != INVALID_PIECE
      && opposite_colours(pawn_piece, m_pieces[capture2]) && !is_king(m_pieces[capture2])) {
      if (get_square_row(capture2) == RANK_1 || get_square_row(capture2) == RANK_8) {
        for (const piece_t promote_piece : promote_pieces) {
          result.push_back(promote_capture_move(start, capture2, pawn_piece, promote_piece, m_pieces[capture2]));
        }
      } else {
        result.push_back(capture_move(start, capture2, pawn_piece, m_pieces[capture2]));
      }
    }

    // En-pass capture
    if (m_en_passant != INVALID_SQUARE) {
      if (capture1 == m_en_passant && m_pieces[capture1] == INVALID_PIECE) {
        result.push_back(en_passant_move(start, m_en_passant, pawn_piece));
      }
      if (capture2 == m_en_passant && m_pieces[capture2] == INVALID_PIECE) {
        result.push_back(en_passant_move(start, m_en_passant, pawn_piece));
      }
    }
  }

  // King
  const square_t start = m_positions[king_piece][0];
  for (const int offset : {-11, -10, -9, -1, 1, 9, 10, 11}) {
    const square_t cur_square = start + offset;
    const piece_t piece = m_pieces[cur_square];
    if (valid_square(cur_square)) {
      if (piece == INVALID_PIECE) {
        result.push_back(quiet_move(start, cur_square, king_piece));
      } else if (opposite_colours(king_piece, piece) && !is_king(piece)) {
        result.push_back(capture_move(start, cur_square, king_piece, piece));
      }
    }
  }

  // Castling
  if (side == WHITE) {
    const bool d1_attacked = square_attacked(D1, BLACK);
    const bool e1_attacked = square_attacked(E1, BLACK);
    const bool f1_attacked = square_attacked(F1, BLACK);
    if (m_castle_state & WHITE_SHORT && !e1_attacked && !f1_attacked
      && m_pieces[F1] == INVALID_PIECE && m_pieces[G1] == INVALID_PIECE) {
      result.push_back(castle_move(E1, G1, WHITE_KING, SHORT_CASTLE_MOVE));
    }
    if (m_castle_state & WHITE_LONG && !e1_attacked && !d1_attacked
      && m_pieces[D1] == INVALID_PIECE && m_pieces[C1] == INVALID_PIECE && m_pieces[B1] == INVALID_PIECE) {
      result.push_back(castle_move(E1, C1, WHITE_KING, LONG_CASTLE_MOVE));
    }
  } else if (side == BLACK) {
    const bool d8_attacked = square_attacked(D8, WHITE);
    const bool e8_attacked = square_attacked(E8, WHITE);
    const bool f8_attacked = square_attacked(F8, WHITE);
    if (m_castle_state & BLACK_SHORT && !e8_attacked && !f8_attacked
      && m_pieces[F8] == INVALID_PIECE && m_pieces[G8] == INVALID_PIECE) {
      result.push_back(castle_move(E8, G8, BLACK_KING, SHORT_CASTLE_MOVE));
    }
    if (m_castle_state & BLACK_LONG && !e8_attacked && !d8_attacked
      && m_pieces[D8] == INVALID_PIECE && m_pieces[C8] == INVALID_PIECE && m_pieces[B8] == INVALID_PIECE) {
      result.push_back(castle_move(E8, C8, BLACK_KING, LONG_CASTLE_MOVE));
    }
  }

  ASSERT(result.size() <= MAX_POSITION_MOVES);
  return m_move_cache[m_hash] = result;
}

std::vector<move_t> Board::legal_moves() const noexcept {
  std::vector<move_t> result;
  Board tmp = *this;
  for (const move_t move : tmp.pseudo_moves()) {
    if (tmp.make_move(move))
      result.push_back(move);
    tmp.unmake_move();
  }
  return result;
}

inline void Board::remove_piece(const square_t sq) noexcept {
  INFO("Removing piece on square %s (%u)", string_from_square(sq).c_str(), sq);
  const piece_t piece = m_pieces[sq];
  ASSERT_MSG(valid_piece(piece), "Removing invalid piece (%u)!", piece);
  m_pieces[sq] = INVALID_PIECE;
  auto &piece_list = m_positions[piece];
  const auto &last_idx = piece_list.begin() + m_num_pieces[piece];
  const auto &this_idx = std::find(piece_list.begin(), last_idx, sq);
  ASSERT_MSG(this_idx != last_idx, "Removed piece (%d) not in piece_list", piece);
  m_num_pieces[piece]--;
  std::swap(*this_idx, *(last_idx - 1));
  m_hash ^= piece_hash[sq][piece];
}

inline void Board::add_piece(const square_t sq, const piece_t piece) noexcept {
  INFO("Adding piece (%c) to square %s", char_from_piece(piece), string_from_square(sq).c_str());
  ASSERT_MSG(valid_piece(piece), "Adding invalid piece!");
  ASSERT_MSG(m_pieces[sq] == INVALID_PIECE, "Adding piece would overwrite existing piece (%d)!", m_pieces[sq]);
  m_pieces[sq] = piece;
  m_positions[piece][m_num_pieces[piece]] = sq;
  m_num_pieces[piece]++;
  m_hash ^= piece_hash[sq][piece];
}

inline void Board::set_castle_state(const castle_t state) noexcept {
  INFO("Setting castle state to %u", state);
  m_hash ^= castle_hash[m_castle_state] ^ castle_hash[state];
  m_castle_state = state;
}

inline void Board::set_en_passant(const square_t sq) noexcept {
  INFO("Setting en passant to %s", string_from_square(sq).c_str());
  m_hash ^= enpas_hash[m_en_passant] ^ enpas_hash[sq];
  m_en_passant = sq;
}

inline void Board::move_piece(const square_t from, const square_t to) noexcept {
  INFO("Moving piece from %s to %s", string_from_square(from).c_str(), string_from_square(to).c_str());
  ASSERT_MSG(m_pieces[to] == INVALID_PIECE, "Attempted to move to occupied square");
  const piece_t piece = m_pieces[from];
  m_pieces[from] = INVALID_PIECE;
  m_pieces[to] = piece;
  auto &piece_list = m_positions[piece];
  const auto &last_idx = piece_list.begin() + m_num_pieces[piece];
  const auto &this_idx = std::find(piece_list.begin(), last_idx, from);
  ASSERT_MSG(this_idx != last_idx, "Moved piece not in piece_list");
  *this_idx = to;
  m_hash ^= piece_hash[from][piece] ^ piece_hash[to][piece];
}

inline void Board::update_castling(const square_t sq, const piece_t moved) noexcept {
  INFO("Updating castling with from = %s", string_from_square(sq).c_str());
  if (!is_castle(moved)) return;
  m_hash ^= castle_hash[m_castle_state];
  if (sq == E1 || sq == A1)
    m_castle_state &= ~WHITE_LONG;
  if (sq == E1 || sq == H1)
    m_castle_state &= ~WHITE_SHORT;
  if (sq == E8 || sq == A8)
    m_castle_state &= ~BLACK_LONG;
  if (sq == E8 || sq == H8)
    m_castle_state &= ~BLACK_SHORT;
  m_hash ^= castle_hash[m_castle_state];
}

inline void Board::switch_colours() noexcept {
  INFO("Switching colours");
  m_next_move_colour ^= 1;
  m_hash ^= side_hash;
}

bool Board::make_move(const move_t move) noexcept {
  INFO("=====================================================================================");
  const MoveFlag flag = move_flag(move);
  const square_t from = move_from(move), to = move_to(move);
  const piece_t moved = moved_piece(move);
  INFO("%s", to_string().c_str());
  INFO("Making move from %s to %s", string_from_square(from).c_str(), string_from_square(to).c_str());
  INFO("Move flag is %s", string_from_flag(flag).c_str());
  INFO("Promoted: %d, Captured: %d", move_promoted(move), move_captured(move));
  const bool cur_side = m_next_move_colour, other_side = !cur_side;

  // Bookkeeping
  history_t entry;
  entry.move = move;
  entry.castle_state = m_castle_state;
  entry.en_passant = m_en_passant;
  entry.fifty_move = m_fifty_move;
  entry.hash = m_hash;
  m_history.push_back(entry);
  m_half_move++;

  if (move_promoted(move)) {
    if (move_captured(move))
      remove_piece(to);
    add_piece(to, promoted_piece(move));
    remove_piece(from);
    set_en_passant(INVALID_SQUARE);
  } else if (move_castled(move)) {
    if (cur_side == WHITE) {
      if (flag == SHORT_CASTLE_MOVE) {
        move_piece(E1, G1);
        move_piece(H1, F1);
      } else {
        move_piece(E1, C1);
        move_piece(A1, D1);
      }
      const castle_t new_castle_state = m_castle_state & ~(WHITE_LONG | WHITE_SHORT);
      set_castle_state(new_castle_state);
    } else {
      if (flag == SHORT_CASTLE_MOVE) {
        move_piece(E8, G8);
        move_piece(H8, F8);
      } else {
        move_piece(E8, C8);
        move_piece(A8, D8);
      }
      const castle_t new_castle_state = m_castle_state & ~(BLACK_LONG | BLACK_SHORT);
      set_castle_state(new_castle_state);
    }
    set_en_passant(INVALID_SQUARE);
  } else {
    const square_t enpas_square = (cur_side == WHITE) ? (to - 10) : (to + 10);
    if (flag == DOUBLE_PAWN_MOVE) {
      set_en_passant(enpas_square);
    } else {
      set_en_passant(INVALID_SQUARE);
    }
    if (flag == QUIET_MOVE) {
      INFO("Handling quiet move");
      move_piece(from, to);
      update_castling(from, moved);
    } else if (flag == DOUBLE_PAWN_MOVE) {
      INFO("Handling double pawn move");
      move_piece(from, to);
    } else if (flag == CAPTURE_MOVE) {
      INFO("Handling capture move");
      remove_piece(to);
      move_piece(from, to);
      update_castling(from, moved);
    } else if (flag == EN_PASSANT_MOVE) {
      INFO("Handling en-passant move");
      remove_piece(enpas_square);
      move_piece(from, to);
    }
  }
  if (move_captured(move) || is_pawn(moved_piece(move)))
    m_fifty_move = 0;
  else
    m_fifty_move++;

  switch_colours();
  const piece_t king_piece = (cur_side == WHITE) ? WHITE_KING : BLACK_KING;
  INFO("Is %s king attacked by %s?", (cur_side == WHITE) ? "white" : "black", (other_side == WHITE) ? "white" : "black");
  INFO("\n%s", to_string().c_str());
  const bool valid = !square_attacked(m_positions[king_piece][0], other_side);
  INFO("%s king %s attacked", (cur_side == WHITE) ? "White" : "Black", valid ? "is not" : "is");
  if (valid) {
    validate_board();
    INFO("=====================================================================================");
    return true;
  }
  INFO("=====================================================================================");
  return false;
}

void Board::unmake_move() noexcept {
  INFO("=====================================================================================");
  ASSERT_MSG(!m_history.empty(), "Trying to unmake move from starting position");
  const history_t entry = m_history.back();
  m_history.pop_back();
  const move_t move = entry.move;
  const hash_t last_hash = entry.hash;
  set_castle_state(entry.castle_state);
  set_en_passant(entry.en_passant);
  m_fifty_move = entry.fifty_move;
  ASSERT_MSG(m_half_move > 0, "Unmaking first move");
  m_half_move--;
  switch_colours();
  const bool cur_side = m_next_move_colour, other_side = !cur_side;

  const MoveFlag flag = move_flag(move);
  const square_t from = move_from(move), to = move_to(move);
  INFO("%s", to_string().c_str());
  INFO("Unmaking move from %s to %s", string_from_square(from).c_str(), string_from_square(to).c_str());
  INFO("Move flag is %s", string_from_flag(flag).c_str());
  INFO("Promoted: %d, Captured: %d", move_promoted(move), move_captured(move));

  if (move_promoted(move)) {
    remove_piece(to);
    add_piece(from, moved_piece(move));
    if (move_captured(move))
      add_piece(to, captured_piece(move));
  } else if (move_castled(move)) {
    if (cur_side == WHITE) {
      if (flag == SHORT_CASTLE_MOVE) {
        move_piece(G1, E1);
        move_piece(F1, H1);
      } else {
        move_piece(C1, E1);
        move_piece(D1, A1);
      }
    } else {
      if (flag == SHORT_CASTLE_MOVE) {
        move_piece(G8, E8);
        move_piece(F8, H8);
      } else {
        move_piece(C8, E8);
        move_piece(D8, A8);
      }
    }
  } else {
    move_piece(to, from);
    if (move_captured(move)) {
      const square_t en_pas_sq = (cur_side == WHITE) ? m_en_passant - 10 : m_en_passant + 10;
      const square_t captured_sq = (flag == CAPTURE_MOVE) ? to : en_pas_sq;
      add_piece(captured_sq, captured_piece(move));
    }
  }

  ASSERT_MSG(m_hash == last_hash, "Hash did not match history entry's hash");
  validate_board();
  INFO("=====================================================================================");
}

void print_move_list(const std::vector<move_t> &move_list) {
  for (const move_t move : move_list) {
    std::cout << string_from_move(move) << ", ";
  }
  std::cout << std::endl;
}
