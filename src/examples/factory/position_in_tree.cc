#include "position_in_tree.hh"

#include <ostream>
#include <unistdx/net/ipv4_addr>

template <class Addr>
asc::position_in_tree<Addr>::position_in_tree(
	pos_type linear_pos,
	pos_type fanout
) noexcept {
	pos_type l = 0;
	pos_type n = 1; // no. of nodes in the layer l
	while (linear_pos > n) {
		linear_pos -= n;
		++l;
		n *= fanout;
	}
	this->_layer = l;
	this->_offset = linear_pos - 1;
	this->_fanout = fanout;
}

template <class Addr>
typename asc::position_in_tree<Addr>::pos_type
asc::position_in_tree<Addr>::to_position_in_address_range() const noexcept {
	pos_type pos = 0;
	pos_type l = 0;
	pos_type n = 1; // no. of nodes in the layer l
	while (l < this->_layer) {
		pos += n;
		++l;
		n *= this->_fanout;
	}
	pos += this->_offset;
	return pos;
}

template <class Addr>
typename asc::position_in_tree<Addr>::pos_type
asc::position_in_tree<Addr>::num_nodes() const noexcept {
	pos_type n = 1; // no. of nodes in the layer l
	const pos_type nl = this->_layer;
	const pos_type fn = this->_fanout;
	for (pos_type l=0; l<nl; ++l) {
		n *= fn;
	}
	return n;
}

template <class Addr>
asc::position_in_tree<Addr>&
asc::position_in_tree<Addr>::operator++() noexcept {
	if (this->_offset == this->num_nodes() - 1) {
		++this->_layer;
		this->_offset = 0;
	} else {
		++this->_offset;
	}
	return *this;
}

template <class Addr>
asc::position_in_tree<Addr>&
asc::position_in_tree<Addr>::operator--() noexcept {
	if (this->_layer == 0 && this->_offset == 0) {
		return *this;
	}
	if (this->_offset == 0) {
		--this->_layer;
		this->_offset = this->num_nodes() - 1;
	} else {
		--this->_offset;
	}
	return *this;
}

template <class Addr>
std::ostream&
asc::operator<<(std::ostream& out, const position_in_tree<Addr>& rhs) {
	return out << '('
		<< rhs._layer << ','
		<< rhs._offset << ','
		<< rhs._fanout << ')';
}

template class asc::position_in_tree<sys::ipv4_addr>;

template std::ostream&
asc::operator<<(
	std::ostream& out,
	const position_in_tree<sys::ipv4_addr>& rhs
);
