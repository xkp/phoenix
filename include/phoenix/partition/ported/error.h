#pragma once

#include <iostream>
#include <vector>
#include <boost/property_tree/ptree.hpp>

typedef boost::property_tree::ptree property_tree;

enum function_location_t {
	FL_PROJECT = 0,	// normal json projects
	FL_LOCAL = 1,
	FL_PUBLIC = 2, // public functions
	FL_BINARY = 3, // binary projects (referenced from scenes)
	FL_SCENE = 4,	// scenes
	FL_USER_VARIATION = 5, // variation folder in the current workspace
};


struct stack_entry
{
	stack_entry(const std::string& fnId_, function_location_t location_, int node_id_) :
		fnId(fnId_),
		location(location_),
		node_id(node_id_)
	{}

	std::string fnId;
	function_location_t location;
	int node_id;
};

typedef std::vector<stack_entry> stack_trace;

struct cancel_render_exception
{
};

struct solver_error
{
	solver_error():
		_id(-1),
		_data(),
		_stack(),
		_instruction_id(0),
		_original_instruction_id(-1),
		_is_bug(false),
		_is_warning(false)
	{
	}

	solver_error(int id, const property_tree& data):
		_id(id),
		_data(data),
		_stack(),
		_instruction_id(0),
		_original_instruction_id(-1),
		_is_bug(false),
		_is_warning(false)
	{
		//_data.add("id", id);
		//_data.add_child("data", data);
	}

	solver_error(int id, const std::string& message):
		_id(id),
		_data(),
		_stack(),
		_message(message),
		_instruction_id(0),
		_original_instruction_id(-1),
		_is_bug(false),
		_is_warning(false)
	{
	}

	solver_error(int id, const std::string& message, const property_tree& data):
		_id(id),
		_message(message),
		_data(data),
		_instruction_id(0),
		_original_instruction_id(-1),
		_stack(),
		_is_bug(false),
		_is_warning(false)
	{
	}

	bool valid() const
	{
		return _id >= 0;
	}

	/*bool valid()
	{
		return _data.size() > 0;
	}*/

	void print()
	{
		std::cerr << "error " << _id;
		if (_instruction_id > 0)
			std::cerr << ", node: " << _instruction_id;
		if (_message.size() > 0)
			std::cerr << ", " << _message;

		/*auto id = _data.get_optional<int>("id");
		std::cout << "error " << id.get_value_or(-1);

		auto data_child = data().get_child("data");
		if (!data_child.empty())
		{

			auto node_id = data_child.get_optional<std::string>("node_id");
			if (node_id)
				std::cout << ", node: " << node_id.get();

			auto message = data_child.get_optional<std::string>("message");
			if (message)
				std::cout << ", " << message.get();

		}*/

		std::cerr << std::endl;
	}

	property_tree to_ptree() const
	{
		property_tree ptree;

		ptree.add("id", _instruction_id);
		//if (_original_node_id != _node_id)
		//	ptree.add("org_id", _original_node_id);

		property_tree error;
		error.add("id", _id);

		error.add("inst_id", _instruction_id);
		if (_original_instruction_id != _instruction_id)
			error.add("org_inst_id", _original_instruction_id);

		if (!_instruction_type.empty())
			error.add("instruction_type", _instruction_type);

		error.add("message", _message);

		if (_is_warning)
			error.add("is_warning", _is_warning);

		if (_is_bug)
			error.add("is_bug", _is_bug);

		error.add_child("data", _data);

		if (!_stack.empty())
		{
			property_tree& functions = error.add_child("stack", property_tree());
			// ignore the first stack item (the root function)
			for (int i=1; i<_stack.size(); i++)
			{
				auto& entry = _stack[i];
				property_tree value;
				value.add("fnId", entry.fnId);
				value.add("location", entry.location);
				functions.push_back(std::make_pair("", value));
			}
		}

		ptree.add_child("error", error);

		return ptree;
	}

	property_tree& data()
	{
		return _data;
	}

	solver_error& add_data(const std::string& name, const char *value)
	{
		_data.add(name, std::string(value));
		return *this;
	}

	solver_error& add_data(const std::string& name, const std::string& value)
	{
		_data.add(name, value);
		return *this;
	}

	solver_error& add_data(const std::string& name, double value)
	{
		std::stringstream ss;
		ss << value;
		_data.add(name, ss.str());
		return *this;
	}

	template<typename T>
	solver_error& add_data(const std::string& name, T value)
	{
		_data.add(name, std::to_string(value));
		return *this;
	}

	std::string message() const
	{
		return _message;
	}

	solver_error& message(const std::string& value)
	{
		_message = value;
		return *this;
	}

	solver_error& add_message(const std::string& value)
	{
		_message += value;
		return *this;
	}

	solver_error& add_stack(const stack_trace& stack)
	{
		_stack = stack;
		return *this;
	}

	stack_trace& stack()
	{
		return _stack;
	}

	int id() const
	{
		return _id;
	}

	solver_error& set_id(int id)
	{
		_id = id;
		return *this;
	}

	
	bool is_warning() const
	{
		return _is_warning;
	}

	solver_error& is_warning(bool is_warning_)
	{
		_is_warning = is_warning_;
		return *this;
	}

	bool is_bug() const
	{
		return _is_bug;
	}

	solver_error& is_bug(bool bug_)
	{
		_is_bug = bug_;
		return *this;
	}

	int instruction_id()
	{
		return _instruction_id;
	}

	solver_error& instruction_id(int id)
	{
		_instruction_id = id;
		_original_instruction_id = id;
		return *this;
	}

	solver_error& root_instruction_id(int id)
	{
		_instruction_id = id;
		return *this;
	}

	std::string instruction_type()
	{
		return _instruction_type;
	}

	solver_error& instruction_type(const std::string& s)
	{
		_instruction_type = s;
		return *this;
	}

private:
	int _id;
	int _instruction_id;
	int _original_instruction_id;
	bool _is_bug;
	bool _is_warning;
	std::string _instruction_type;
	std::string _message;
	property_tree _data;
	stack_trace _stack;
};

typedef std::function<void(solver_error& error)> error_function;
typedef std::function<void(solver_error& error, bool warning)> error_function2;


typedef std::vector<solver_error> error_list;
typedef std::shared_ptr<error_list> error_list_ref;

