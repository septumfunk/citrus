let class = {
    member = 4
    method = [class](val) { class.member = val; }
};
return class;