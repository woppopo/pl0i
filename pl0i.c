#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

typedef enum
{
	Op_Invalid,
	Op_Load,
	Op_Literal,
	Op_Store,
	Op_Operate,
	Op_Allocate,
	Op_Jump,
	Op_JumpZero,
	Op_Call,
	Op_Intrinsic,
	Op_Label,
	Op_Return,
} OpKind;

typedef struct
{
	OpKind kind;
	int opr1;
	int opr2;
} Op;

bool is_whitespace(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool is_blank_line(char *str)
{
	size_t len = strlen(str);
	for (size_t i = 0; i < len; ++i)
	{
		if (!is_whitespace(str[i]))
		{
			return false;
		}
	}
	return true;
}

Op parse_op(char *line)
{
	char *line_without_whitespace = (char *)malloc(strlen(line) + 1);
	for (size_t i = 0, j = 0; i < strlen(line); i++)
	{
		if (!is_whitespace(line[i]))
		{
			line_without_whitespace[j] = line[i];
			j++;
		}
	}

	Op op;
	char op_name[4];
	sscanf(line_without_whitespace, "(%[^,],%d,%d)", op_name, &op.opr1, &op.opr2);
	free(line_without_whitespace);

	if (strcmp(op_name, "LOD") == 0)
	{
		op.kind = Op_Load;
	}
	else if (strcmp(op_name, "LIT") == 0)
	{
		op.kind = Op_Literal;
	}
	else if (strcmp(op_name, "STO") == 0)
	{
		op.kind = Op_Store;
	}
	else if (strcmp(op_name, "OPR") == 0)
	{
		op.kind = Op_Operate;
	}
	else if (strcmp(op_name, "INT") == 0)
	{
		op.kind = Op_Allocate;
	}
	else if (strcmp(op_name, "JMP") == 0)
	{
		op.kind = Op_Jump;
	}
	else if (strcmp(op_name, "JPC") == 0)
	{
		op.kind = Op_JumpZero;
	}
	else if (strcmp(op_name, "CAL") == 0)
	{
		op.kind = Op_Call;
	}
	else if (strcmp(op_name, "CSP") == 0)
	{
		op.kind = Op_Intrinsic;
	}
	else if (strcmp(op_name, "LAB") == 0)
	{
		op.kind = Op_Label;
	}
	else if (strcmp(op_name, "RET") == 0)
	{
		op.kind = Op_Return;
	}
	else
	{
		fprintf(stderr, "Unknown op: %s\n", op_name);
		exit(1);
	}

	return op;
}

typedef struct
{
	size_t top;
	size_t size;
	int *data;
} Stack;

Stack stack_new(void)
{
	Stack stack;
	stack.top = 0;
	stack.size = 0;
	stack.data = (int *)malloc(0);
	return stack;
}

void stack_free(Stack *stack)
{
	free(stack->data);
	stack->data = NULL;
}

void stack_allocate(Stack *stack, size_t size)
{
	stack->size += size;
	stack->data = (int *)realloc(stack->data, stack->size * sizeof(int));
}

int stack_get(Stack *stack, size_t at)
{
	return stack->data[at];
}

void stack_set(Stack *stack, size_t at, int value)
{
	stack->data[at] = value;
}

void stack_push(Stack *stack, int value)
{
	stack_allocate(stack, 1);
	stack->top++;
	stack->data[stack->top - 1] = value;
}

int stack_pop(Stack *stack)
{
	int value = stack->data[stack->top - 1];
	stack->top--;
	return value;
}

typedef struct
{
	size_t static_link;
	size_t dynamic_link;
	size_t return_address;
} Record;

Record get_record(Stack *stack, size_t base_ptr)
{
	Record record = {
		 .static_link = (size_t)stack_get(stack, base_ptr),
		 .dynamic_link = (size_t)stack_get(stack, base_ptr + 1),
		 .return_address = (size_t)stack_get(stack, base_ptr + 2),
	};
	return record;
}

size_t push_record(Stack *stack, Record record)
{
	size_t base = stack->top;
	stack_allocate(stack, 3);
	stack_push(stack, (int)record.static_link);
	stack_push(stack, (int)record.dynamic_link);
	stack_push(stack, (int)record.return_address);
	return base;
}

void pop_record(Stack *stack, size_t *pc, size_t *bp)
{
	Record record = get_record(stack, *bp);
	stack->top = *bp;
	*bp = record.dynamic_link;
	*pc = record.return_address;
}

size_t base(Stack *stack, size_t base_ptr, size_t level_diff)
{
	if (level_diff == 0)
	{
		return base_ptr;
	}
	else
	{
		Record record = get_record(stack, base_ptr);
		return base(stack, record.static_link, level_diff - 1);
	}
}

size_t value_at(Stack *stack, size_t base_ptr, size_t level_diff, int offset)
{
	return (size_t)((int)base(stack, base_ptr, level_diff) + offset);
}

void run(Op *ops, size_t len)
{
	Record initial_record = {
		 .static_link = 0,
		 .dynamic_link = 0,
		 .return_address = 0,
	};

	Stack stack = stack_new();
	size_t pc = 0;
	size_t bp = push_record(&stack, initial_record);

	while (stack.top != 0)
	{
		if (pc >= len)
		{
			fprintf(stderr, "PC out of bounds: %lu\n", pc);
			exit(1);
		}

		Op op = ops[pc];
		switch (op.kind)
		{
		case Op_Invalid:
		{
			printf("Invalid op at memory %ld\n", pc);
			exit(1);
			break;
		}
		case Op_Load:
		{
			size_t at = value_at(&stack, bp, (size_t)op.opr1, op.opr2);
			int value = stack_get(&stack, at);
			stack_push(&stack, value);
			pc++;
			break;
		}
		case Op_Literal:
		{
			stack_push(&stack, op.opr2);
			pc++;
			break;
		}
		case Op_Store:
		{
			size_t at = value_at(&stack, bp, (size_t)op.opr1, op.opr2);
			int value = stack_pop(&stack);
			stack_set(&stack, at, value);
			pc++;
			break;
		}
		case Op_Operate:
			switch (op.opr2)
			{
			case 0:
			{
				pop_record(&stack, &pc, &bp);
				break;
			}
			case 2:
			{
				int b = stack_pop(&stack);
				int a = stack_pop(&stack);
				stack_push(&stack, a + b);
				break;
			}
			case 3:
			{
				int b = stack_pop(&stack);
				int a = stack_pop(&stack);
				stack_push(&stack, a - b);
				break;
			}
			case 4:
			{
				int b = stack_pop(&stack);
				int a = stack_pop(&stack);
				stack_push(&stack, a * b);
				break;
			}
			case 5:
			{
				int b = stack_pop(&stack);
				int a = stack_pop(&stack);
				stack_push(&stack, a / b);
				break;
			}
			case 6:
			{
				stack_push(&stack, stack_pop(&stack) % 2 == 1 ? 1 : 0);
				break;
			}
			case 8:
			{
				int b = stack_pop(&stack);
				int a = stack_pop(&stack);
				stack_push(&stack, a == b ? 1 : 0);
				break;
			}
			case 9:
			{
				int b = stack_pop(&stack);
				int a = stack_pop(&stack);
				stack_push(&stack, a != b ? 1 : 0);
				break;
			}
			case 10:
			{
				int b = stack_pop(&stack);
				int a = stack_pop(&stack);
				stack_push(&stack, a < b ? 1 : 0);
				break;
			}
			case 11:
			{
				int b = stack_pop(&stack);
				int a = stack_pop(&stack);
				stack_push(&stack, a >= b ? 1 : 0);
				break;
			}
			case 12:
			{
				int b = stack_pop(&stack);
				int a = stack_pop(&stack);
				stack_push(&stack, a > b ? 1 : 0);
				break;
			}
			case 13:
			{
				int b = stack_pop(&stack);
				int a = stack_pop(&stack);
				stack_push(&stack, a <= b ? 1 : 0);
				break;
			}
			default:
			{
				fprintf(stderr, "Unknown opr2: %d\n", op.opr2);
				exit(1);
			}
			}
			pc++;
			break;
		case Op_Allocate:
		{
			stack_allocate(&stack, (size_t)op.opr2);
			pc++;
			break;
		}
		case Op_Jump:
		{
			pc = (size_t)op.opr2;
			break;
		}
		case Op_JumpZero:
		{
			if (stack_pop(&stack) == 0)
			{
				pc = (size_t)op.opr2;
			}
			else
			{
				pc++;
			}
			break;
		}
		case Op_Call:
		{
			// Set the record with the specified level difference as a static link
			size_t target_bp = base(&stack, bp, (size_t)op.opr1);
			Record target_record = get_record(&stack, target_bp);
			size_t static_link = target_record.static_link;

			Record new_record = {
				 .static_link = static_link,
				 .dynamic_link = bp,
				 .return_address = pc + 1,
			};

			bp = push_record(&stack, new_record);
			pc = (size_t)op.opr2;
			break;
		}
		case Op_Intrinsic:
			switch (op.opr2)
			{
			case 0:
			{
				// Input a number and push it to the stack.
				int value;
				printf("Input: ");
				scanf("%d", &value);
				stack_push(&stack, value);
				break;
			}
			case 1:
			{
				// Pop a value from the stack and print it.
				printf("Output: %d", stack_pop(&stack));
				break;
			}
			case 2:
			{
				// Print a new line.
				printf("\n");
				break;
			}
			}
			pc++;
			break;
		case Op_Label:
		{
			// noop
			pc++;
			break;
		}
		case Op_Return:
		{
			// Return to the caller,
			// remove the arguments when the callee was called,
			// and push the return value.
			int ret = stack_pop(&stack);
			pop_record(&stack, &pc, &bp);
			stack.top -= (size_t)op.opr2;
			stack_push(&stack, ret);
			break;
		}
		}
	}

	stack_free(&stack);
}

size_t count_lines(FILE *fp)
{
	size_t lines = 0;

	int ch;
	while ((ch = fgetc(fp)) != EOF)
	{
		if (ch == '\n')
			lines++;
	}
	fseek(fp, 0, SEEK_SET);

	return lines + 1; // The last line doesn't contain '\n'
}

int main(int argc, char **argv)
{
	if (argc != 2)
	{
		printf("Usage: %s <filename>\n", argv[0]);
		return 1;
	}

	FILE *fp = fopen(argv[1], "r");
	if (fp == NULL)
	{
		printf("Error: cannot open file %s\n", argv[1]);
		return 1;
	}

	// Count lines in a file to determine allocation size of opcodes and labels
	size_t lines_count = count_lines(fp);

	// Labels: Mapping of operand values to instruction addresses
	size_t *labels = (size_t *)malloc(lines_count * sizeof(size_t));

	// Opcodes: This is not shared with RAM. It can be considered as Harvard architecture.
	Op *opcodes = (Op *)malloc(lines_count * sizeof(Op));
	size_t opcodes_count = 0;

	char line[20];
	while (fgets(line, sizeof(line) / sizeof(char), fp) != NULL)
	{
		// Don't parse when it is blank line
		if (is_blank_line(line))
		{
			continue;
		}

		Op op = parse_op(line);

		// Label maps an operand value (opr2) of a jump instruction
		// to its own instruction address
		if (op.kind == Op_Label)
		{
			labels[op.opr2] = opcodes_count;
		}

		opcodes[opcodes_count] = op;
		opcodes_count++;
	}
	fclose(fp);

	// Convert an operand value of a jump instruction
	// to an corresponding instruction number.
	for (size_t i = 0; i < opcodes_count; ++i)
	{
		Op *op = &opcodes[i];
		if (op->kind == Op_Jump || op->kind == Op_JumpZero || op->kind == Op_Call)
		{
			op->opr2 = (int)labels[op->opr2];
		}
	}
	free(labels);

	// RUN.
	run(opcodes, opcodes_count);
	free(opcodes);
	return 0;
}
