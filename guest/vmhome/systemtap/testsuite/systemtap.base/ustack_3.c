void mark()
{
}

int my_func_name_is_very_longlonglonglonglonglonglonglonglonglonglong(int depth)
{
   int sum;

   if (depth <= 0) {
      mark();
      return 0;
   }

   sum =  my_func_name_is_very_longlonglonglonglonglonglonglonglonglonglong(depth - 1);
   sum += depth;
   return sum;
}

int main(void) {
    my_func_name_is_very_longlonglonglonglonglonglonglonglonglonglong(200);
    return 0;
}
